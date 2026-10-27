/*
 * Native replacement for Manhunt's statically linked XAPI gamepad calls.
 *
 * Why: the XDK's XPP/USB stack needs trapped OHCI registers. The x86-64 POSIX
 * runtime has no MMIO decoder, and Horizon (Eden) does not even fault on an
 * unmapped access, so on both targets the pad is provided at the API level.
 *
 * Addresses (reports/FEASIBILITY.md, from the XBE's .XTLID table + 5849 IDs):
 *   0x0029F214 XPP_GamepadType     {current, change, previous} port masks
 *   0x002A0170 XInputOpen          (type, port, slot, params)  ret 0x10
 *   0x002A01C6 XInputClose         (handle)                    ret 4
 *   0x002A01D2 XInputGetState      (handle, XINPUT_STATE *)    ret 8
 *   0x002A0245 XInputSetState      (handle, XINPUT_FEEDBACK *) ret 8
 * The four functions are excluded from generation (game/manual_functions.json),
 * so these definitions link in their place for direct and indirect calls.
 * XGetDevices (0x002A029F) stays lifted: it reads XPP_GamepadType.
 *
 * Input source: manhunt_pad_sample() (pad.c), scripted on the host and the
 * real controller on Switch.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "recomp_types.h"
#include "pad.h"

#define XPP_GAMEPAD_TYPE   0x0029F214u
#define ERROR_SUCCESS      0u
#define ERROR_IO_PENDING   997u
#define PAD_HANDLE_BASE    0x5EED0000u   /* opaque to the game: stored, passed back */

static uint32_t s_packet;

/* stdcall: the return address is at [esp], arguments follow; the callee pops
 * both, as the lifted `ret N` does (recomp_types.h calling convention). */
static uint32_t arg(int i) { return MEM32(g_esp + 4 + 4 * (uint32_t)i); }
static void ret_stdcall(uint32_t value, uint32_t arg_bytes)
{
    g_eax = value;
    g_esp += 4 + arg_bytes;
}

void manhunt_input_init(void)
{
    /* One gamepad in port 0, reported as just inserted. */
    MEM32(XPP_GAMEPAD_TYPE + 0) = 1;
    MEM32(XPP_GAMEPAD_TYPE + 4) = 1;
    MEM32(XPP_GAMEPAD_TYPE + 8) = 0;
    printf("[MH-BOOT] input: native XInput, pad in port 0\n");
}

/* XInputOpen(PXPP_DEVICE_TYPE, DWORD port, DWORD slot, PXINPUT_POLLING_PARAMETERS) */
void sub_002A0170(void)
{
    uint32_t port = arg(1);
    uint32_t handle = (arg(0) == XPP_GAMEPAD_TYPE && port == 0) ? PAD_HANDLE_BASE | port : 0;
    printf("[MH-INPUT] XInputOpen port=%u -> %08X\n", port, handle);
    ret_stdcall(handle, 16);
}

/* XInputClose(HANDLE) */
void sub_002A01C6(void)
{
    ret_stdcall(0, 4);
}

/* XInputGetState(HANDLE, PXINPUT_STATE): DWORD dwPacketNumber; XINPUT_GAMEPAD
 * { WORD wButtons; BYTE bAnalogButtons[8]; SHORT sThumbLX, sThumbLY,
 * sThumbRX, sThumbRY; } */
void sub_002A01D2(void)
{
    uint32_t handle = arg(0), state = arg(1);
    ManhuntPad pad;
    int i;

    if ((handle & 0xFFFF0000u) != PAD_HANDLE_BASE || !state) {
        ret_stdcall(1167 /* ERROR_DEVICE_NOT_CONNECTED */, 8);
        return;
    }
    manhunt_pad_sample(&pad);
    MEM32(state + 0) = ++s_packet;
    MEM16(state + 4) = pad.buttons;
    for (i = 0; i < 8; i++)
        MEM8(state + 6 + (uint32_t)i) = pad.analog[i];
    MEM16(state + 14) = (uint16_t)pad.lx;
    MEM16(state + 16) = (uint16_t)pad.ly;
    MEM16(state + 18) = (uint16_t)pad.rx;
    MEM16(state + 20) = (uint16_t)pad.ry;
    ret_stdcall(ERROR_SUCCESS, 8);
}

/* XInputSetState(HANDLE, PXINPUT_FEEDBACK): asynchronous on the console. The
 * request is reported pending and its header already completed, so a title
 * polling dwStatus sees it finish. No rumble yet. */
void sub_002A0245(void)
{
    uint32_t feedback = arg(1);
    if (feedback)
        MEM32(feedback + 0) = ERROR_SUCCESS;     /* XINPUT_FEEDBACK_HEADER.dwStatus */
    ret_stdcall(ERROR_IO_PENDING, 8);
}
