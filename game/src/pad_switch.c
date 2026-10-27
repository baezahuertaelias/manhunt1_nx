/*
 * Switch pad: the MANHUNT_PAD_SCRIPT player when a script is configured
 * (sdmc:/manhunt.env), otherwise the real controller via libnx,
 * mapped to the Xbox pad (A/B/X/Y by position: Switch B = Xbox A).
 */
#include <string.h>

#include <switch.h>

#include "pad.h"

static PadState s_pad;
static int s_pad_ready;

static uint8_t analog(u64 held, u64 bit) { return (held & bit) ? 255 : 0; }

void manhunt_pad_sample(ManhuntPad *out)
{
    if (manhunt_pad_script(out))
        return;
    if (!s_pad_ready) {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&s_pad);
        s_pad_ready = 1;
    }
    padUpdate(&s_pad);
    u64 h = padGetButtons(&s_pad);
    HidAnalogStickState l = padGetStickPos(&s_pad, 0), r = padGetStickPos(&s_pad, 1);

    memset(out, 0, sizeof *out);
    if (h & HidNpadButton_Up)     out->buttons |= MH_PAD_UP;
    if (h & HidNpadButton_Down)   out->buttons |= MH_PAD_DOWN;
    if (h & HidNpadButton_Left)   out->buttons |= MH_PAD_LEFT;
    if (h & HidNpadButton_Right)  out->buttons |= MH_PAD_RIGHT;
    if (h & HidNpadButton_Plus)   out->buttons |= MH_PAD_START;
    if (h & HidNpadButton_Minus)  out->buttons |= MH_PAD_BACK;
    if (h & HidNpadButton_StickL) out->buttons |= MH_PAD_LTHUMB;
    if (h & HidNpadButton_StickR) out->buttons |= MH_PAD_RTHUMB;
    out->analog[MH_A]     = analog(h, HidNpadButton_B);   /* bottom face button */
    out->analog[MH_B]     = analog(h, HidNpadButton_A);   /* right */
    out->analog[MH_X]     = analog(h, HidNpadButton_Y);   /* left */
    out->analog[MH_Y]     = analog(h, HidNpadButton_X);   /* top */
    out->analog[MH_BLACK] = analog(h, HidNpadButton_R);
    out->analog[MH_WHITE] = analog(h, HidNpadButton_L);
    out->analog[MH_LT]    = analog(h, HidNpadButton_ZL);
    out->analog[MH_RT]    = analog(h, HidNpadButton_ZR);
    out->lx = (int16_t)l.x; out->ly = (int16_t)l.y;
    out->rx = (int16_t)r.x; out->ry = (int16_t)r.y;
}
