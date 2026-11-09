/*
 * Switch pad: the MANHUNT_PAD_SCRIPT player when a script is configured
 * (sdmc:/switch/manhunt1_nx/manhunt.env), otherwise the real controller via
 * libnx, mapped to the Xbox pad. A and B follow their labels (Switch A = Xbox A,
 * the user's choice, 2026-11-05); X and Y are by position (Switch Y = Xbox X).
 *
 * The title polls XInputGetState once per game frame, and on the console the
 * CPU rasteriser runs at well under 1 frame per second, so a button tap
 * (100-200 ms) almost always falls between two polls. A poller thread reads
 * the pad every 4 ms; every button pressed since the game's last poll is
 * latched and delivered with the next one (held state is unchanged).
 * MANHUNT_PAD_LATCH=0 gives the old "state at the moment of the poll".
 * MANHUNT_PAD_HOLD_POLLS=N (default 1) keeps a latched tap "pressed" for N
 * consecutive polls, for a title whose menu needs a press to persist across
 * several (very slow) frames; with N=1 a tap is delivered on one poll only.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "pad.h"

static PadState s_pad;                 /* configured on the first game poll */
static int s_pad_ready;
static int s_latch_on = 1;
static Mutex s_lock;
static ManhuntPad s_latest;            /* last 4 ms sample */
static ManhuntPad s_latched;           /* OR of every sample since the last game poll */
static int s_hold_polls = 1;           /* MANHUNT_PAD_HOLD_POLLS */
static uint8_t s_hold_btn[16];         /* polls left to deliver, per wButtons bit */
static uint8_t s_hold_an[8];           /* ... per analog button */

static uint8_t analog(u64 held, u64 bit) { return (held & bit) ? 255 : 0; }

static void fill_from(PadState *p, ManhuntPad *out)
{
    u64 h;
    HidAnalogStickState l, r;

    padUpdate(p);
    h = padGetButtons(p);
    l = padGetStickPos(p, 0);
    r = padGetStickPos(p, 1);

    memset(out, 0, sizeof *out);
    if (h & HidNpadButton_Up)     out->buttons |= MH_PAD_UP;
    if (h & HidNpadButton_Down)   out->buttons |= MH_PAD_DOWN;
    if (h & HidNpadButton_Left)   out->buttons |= MH_PAD_LEFT;
    if (h & HidNpadButton_Right)  out->buttons |= MH_PAD_RIGHT;
    if (h & HidNpadButton_Plus)   out->buttons |= MH_PAD_START;
    if (h & HidNpadButton_Minus)  out->buttons |= MH_PAD_BACK;
    if (h & HidNpadButton_StickL) out->buttons |= MH_PAD_LTHUMB;
    if (h & HidNpadButton_StickR) out->buttons |= MH_PAD_RTHUMB;
    out->analog[MH_A]     = analog(h, HidNpadButton_A);   /* by label: right */
    out->analog[MH_B]     = analog(h, HidNpadButton_B);   /* by label: bottom */
    out->analog[MH_X]     = analog(h, HidNpadButton_Y);   /* left */
    out->analog[MH_Y]     = analog(h, HidNpadButton_X);   /* top */
    out->analog[MH_BLACK] = analog(h, HidNpadButton_R);
    out->analog[MH_WHITE] = analog(h, HidNpadButton_L);
    out->analog[MH_LT]    = analog(h, HidNpadButton_ZL);
    out->analog[MH_RT]    = analog(h, HidNpadButton_ZR);
    out->lx = (int16_t)l.x; out->ly = (int16_t)l.y;
    out->rx = (int16_t)r.x; out->ry = (int16_t)r.y;
}

static unsigned analog_mask(const ManhuntPad *p)
{
    unsigned m = 0;
    for (int i = 0; i < 8; i++)
        if (p->analog[i])
            m |= 1u << i;
    return m;
}

/* 4 ms poller. Own PadState (a PadState is not shared between threads). Logs
 * the controller state at start and every button change (first 300 lines), so
 * a hardware run shows whether the console sees the Joy-Con at all. */
static void *pad_poller(void *unused)
{
    PadState p;
    ManhuntPad cur, prev;
    int logged = 0, first = 1;
    (void)unused;

    padInitializeDefault(&p);
    memset(&prev, 0, sizeof prev);
    for (;;) {
        fill_from(&p, &cur);
        if (first) {
            printf("[MH-PAD] poller up: connected=%d handheld=%d\n", (int)padIsConnected(&p), (int)padIsHandheld(&p));
            first = 0;
        }
        mutexLock(&s_lock);
        s_latest = cur;
        s_latched.buttons |= cur.buttons;
        for (int i = 0; i < 8; i++)
            s_latched.analog[i] |= cur.analog[i];
        mutexUnlock(&s_lock);
        if ((cur.buttons != prev.buttons || analog_mask(&cur) != analog_mask(&prev)) && logged < 300) {
            logged++;
            printf("[MH-PAD] buttons=0x%04x analog=0x%02x connected=%d\n", cur.buttons, analog_mask(&cur),
                   (int)padIsConnected(&p));
        }
        prev = cur;
        svcSleepThread(4000000ll);
    }
    return NULL;
}

void manhunt_pad_sample(ManhuntPad *out)
{
    if (manhunt_pad_script(out))
        return;
    if (!s_pad_ready) {
        const char *e = getenv("MANHUNT_PAD_LATCH");
        const char *hp = getenv("MANHUNT_PAD_HOLD_POLLS");
        s_latch_on = !(e && *e == '0');
        s_hold_polls = hp ? atoi(hp) : 1;
        if (s_hold_polls < 1) s_hold_polls = 1;
        if (s_hold_polls > 10) s_hold_polls = 10;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&s_pad);
        mutexInit(&s_lock);
        s_pad_ready = 1;
        if (s_latch_on) {
            pthread_t t;
            if (pthread_create(&t, NULL, pad_poller, NULL) == 0)
                pthread_detach(t);
            else
                s_latch_on = 0;           /* no poller: fall back to polling in the game's thread */
        }
    }
    if (!s_latch_on) {
        fill_from(&s_pad, out);
        return;
    }
    mutexLock(&s_lock);
    *out = s_latest;
    /* A newly latched press starts a hold of s_hold_polls polls; every bit with
     * polls left is delivered on this poll. */
    for (int i = 0; i < 16; i++) {
        if (s_latched.buttons & (1u << i))
            s_hold_btn[i] = (uint8_t)s_hold_polls;
        if (s_hold_btn[i]) {
            out->buttons |= (uint16_t)(1u << i);
            s_hold_btn[i]--;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (s_latched.analog[i])
            s_hold_an[i] = (uint8_t)s_hold_polls;
        if (s_hold_an[i]) {
            out->analog[i] = 255;
            s_hold_an[i]--;
        }
    }
    memset(&s_latched, 0, sizeof s_latched);
    mutexUnlock(&s_lock);
}
