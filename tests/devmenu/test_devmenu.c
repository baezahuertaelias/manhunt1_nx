/* Host test of game/src/devmenu.c: the L3+R3 combo toggles once per press, the title never sees L3 or R3 while both
 * are held, other buttons pass through, and with the switch off nothing changes.
 *   cc -std=c11 -Wall -Wextra -fsanitize=undefined -I ../../game/src ../../game/src/devmenu.c test_devmenu.c -o /tmp/d && /tmp/d
 * The switch is read once per process, so the off case runs as a separate process: `MANHUNT_DEVMENU= /tmp/d off`.
 * Scope: the combo only. Whether the menu then opens is a console question (reports/DEVMENU.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devmenu.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s\n", #c); fails++; } } while (0)

static int poll(uint16_t buttons, uint16_t *seen)
{
    ManhuntPad p;
    memset(&p, 0, sizeof p);
    p.buttons = buttons;
    p.analog[MH_A] = 255;
    int toggle = manhunt_devmenu_combo(&p);
    *seen = p.buttons;
    CHECK(p.analog[MH_A] == 255);
    return toggle;
}

int main(int argc, char **argv)
{
    uint16_t seen;
    const uint16_t both = MH_PAD_LTHUMB | MH_PAD_RTHUMB;
    if (argc > 1 && !strcmp(argv[1], "off")) {
        CHECK(poll(both | MH_PAD_UP, &seen) == 0 && seen == (both | MH_PAD_UP));
    } else {
        setenv("MANHUNT_DEVMENU", "1", 1);
        CHECK(poll(MH_PAD_LTHUMB, &seen) == 0 && seen == MH_PAD_LTHUMB);    /* one stick alone passes through */
        CHECK(poll(both | MH_PAD_UP, &seen) == 1 && seen == MH_PAD_UP);     /* the combo toggles, and is hidden */
        CHECK(poll(both | MH_PAD_UP, &seen) == 0 && seen == MH_PAD_UP);     /* held: no second toggle, still hidden */
        CHECK(poll(MH_PAD_RTHUMB, &seen) == 0 && seen == MH_PAD_RTHUMB);    /* released one: it passes again */
        CHECK(poll(both, &seen) == 1 && seen == 0);                         /* pressed again: toggles again */
        CHECK(poll(0, &seen) == 0 && seen == 0);
    }
    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", fails);
    return fails != 0;
}
