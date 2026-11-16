/* Controller quit combo: hold ZL + ZR + Plus + Minus together for
 * MANHUNT_QUIT_HOLD_MS (default 1500) to leave the game. All four are on at
 * once only on purpose (the title uses ZL/ZR to aim and shoot, Plus for the
 * pause menu and Minus for back, never all four together), and the hold time
 * means a brush against them does nothing. Pure logic so it runs on the host;
 * pad_switch.c feeds it every 4 ms sample and main_switch.c does the quit.
 * MANHUNT_QUIT_COMBO=0 turns it off. */
#ifndef MANHUNT_QUIT_COMBO_H
#define MANHUNT_QUIT_COMBO_H

#include "pad.h"

#define MANHUNT_QUIT_HOLD_DEFAULT_MS 1500

static inline int manhunt_quit_combo_down(const ManhuntPad *p)
{
    return (p->buttons & MH_PAD_START) && (p->buttons & MH_PAD_BACK) &&
           p->analog[MH_LT] && p->analog[MH_RT];
}

/* Call once per sample, dt_ms after the previous one. *held_ms is the
 * caller's counter, start it at 0. Returns 1 once, on the sample where the
 * hold reaches hold_ms; letting go resets it. */
static inline int manhunt_quit_combo_step(const ManhuntPad *p, int *held_ms, int dt_ms, int hold_ms)
{
    if (!manhunt_quit_combo_down(p)) {
        *held_ms = 0;
        return 0;
    }
    if (*held_ms >= hold_ms)
        return 0;                     /* already fired for this hold */
    *held_ms += dt_ms;
    return *held_ms >= hold_ms;
}

#endif
