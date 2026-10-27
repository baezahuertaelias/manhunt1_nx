/* Platform-neutral gamepad sample in Xbox terms (XINPUT_GAMEPAD). */
#ifndef MANHUNT_PAD_H
#define MANHUNT_PAD_H

#include <stdint.h>

/* wButtons */
#define MH_PAD_UP     0x0001u
#define MH_PAD_DOWN   0x0002u
#define MH_PAD_LEFT   0x0004u
#define MH_PAD_RIGHT  0x0008u
#define MH_PAD_START  0x0010u
#define MH_PAD_BACK   0x0020u
#define MH_PAD_LTHUMB 0x0040u
#define MH_PAD_RTHUMB 0x0080u

/* bAnalogButtons indices */
enum { MH_A, MH_B, MH_X, MH_Y, MH_BLACK, MH_WHITE, MH_LT, MH_RT };

typedef struct {
    uint16_t buttons;
    uint8_t analog[8];
    int16_t lx, ly, rx, ry;
} ManhuntPad;

/* Fill *out with the current pad state. Host: MANHUNT_PAD_SCRIPT only;
 * Switch: the script when configured, otherwise the real controller. */
void manhunt_pad_sample(ManhuntPad *out);

/* MANHUNT_PAD_SCRIPT player (pad_script.c): 1 if a script is configured. */
int manhunt_pad_script(ManhuntPad *out);

#endif
