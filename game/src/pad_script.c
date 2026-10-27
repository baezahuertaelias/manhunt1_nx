/*
 * Scripted pad for headless runs (host build).
 *
 * MANHUNT_PAD_SCRIPT="<ms>:<button>[+<button>...][:<hold_ms>],..."
 *   ms is milliseconds since the first sample; hold defaults to 150 ms.
 *   Buttons: up down left right start back lthumb rthumb a b x y black white
 *   lt rt. Example: "15000:start,20000:a:300".
 * Every press is logged once as "[MH-INPUT] press <name> at <ms>" so a run's
 * log shows exactly what the game was given.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "pad.h"

#define MAX_STEPS 128

typedef struct {
    unsigned long at, hold;
    uint16_t buttons;
    uint8_t analog_mask;    /* bit i -> bAnalogButtons[i] = 255 */
    char text[48];
    int logged;
} Step;

static Step s_steps[MAX_STEPS];
static int s_count = -1;
static unsigned long s_t0;

static unsigned long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000ul + (unsigned long)(ts.tv_nsec / 1000000);
}

static int parse_button(const char *name, size_t n, Step *st)
{
    static const struct { const char *name; uint16_t bit; int analog; } map[] = {
        {"up", MH_PAD_UP, -1}, {"down", MH_PAD_DOWN, -1}, {"left", MH_PAD_LEFT, -1},
        {"right", MH_PAD_RIGHT, -1}, {"start", MH_PAD_START, -1}, {"back", MH_PAD_BACK, -1},
        {"lthumb", MH_PAD_LTHUMB, -1}, {"rthumb", MH_PAD_RTHUMB, -1},
        {"a", 0, MH_A}, {"b", 0, MH_B}, {"x", 0, MH_X}, {"y", 0, MH_Y},
        {"black", 0, MH_BLACK}, {"white", 0, MH_WHITE}, {"lt", 0, MH_LT}, {"rt", 0, MH_RT},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (strlen(map[i].name) == n && strncasecmp(map[i].name, name, n) == 0) {
            st->buttons |= map[i].bit;
            if (map[i].analog >= 0)
                st->analog_mask |= (uint8_t)(1u << map[i].analog);
            return 1;
        }
    }
    return 0;
}

static void load_script(void)
{
    const char *p = getenv("MANHUNT_PAD_SCRIPT");
    s_count = 0;
    s_t0 = now_ms();
    while (p && *p && s_count < MAX_STEPS) {
        Step st;
        char *end;
        const char *b, *start;
        while (*p == ',' || *p == ' ')
            p++;
        if (!*p)
            break;
        memset(&st, 0, sizeof st);
        st.hold = 150;
        start = p;
        st.at = strtoul(p, &end, 10);
        if (end == p || *end != ':') {
            fprintf(stderr, "[MH-INPUT] bad script entry at \"%.20s\"\n", p);
            break;
        }
        b = end + 1;
        for (;;) {
            size_t n = strcspn(b, "+:, ");
            if (!parse_button(b, n, &st))
                fprintf(stderr, "[MH-INPUT] unknown button \"%.*s\"\n", (int)n, b);
            b += n;
            if (*b != '+')
                break;
            b++;
        }
        if (*b == ':')
            st.hold = strtoul(b + 1, (char **)&b, 10);
        snprintf(st.text, sizeof st.text, "%.*s", (int)(b - start), start);
        s_steps[s_count++] = st;
        p = b;
    }
    if (s_count)
        printf("[MH-INPUT] pad script: %d step(s)\n", s_count);
}

void manhunt_pad_sample(ManhuntPad *out)
{
    unsigned long t;
    memset(out, 0, sizeof *out);
    if (s_count < 0)
        load_script();
    t = now_ms() - s_t0;
    for (int i = 0; i < s_count; i++) {
        Step *st = &s_steps[i];
        if (t < st->at || t >= st->at + st->hold)
            continue;
        out->buttons |= st->buttons;
        for (int k = 0; k < 8; k++)
            if (st->analog_mask & (1u << k))
                out->analog[k] = 255;
        if (!st->logged) {
            st->logged = 1;
            printf("[MH-INPUT] press %s at %lu ms\n", st->text, t);
        }
    }
}
