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
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "pad.h"
#include "devmenu.h"

/* The developer menu's open flag: items are drawn and navigated only while it is non-zero (reports/DEVMENU.md). */
#define DEVMENU_OPEN 0x003D7298u

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
    /* MANHUNT_DEVMENU=1: L3+R3 toggles Rockstar's developer menu by its open flag (reports/DEVMENU.md), here on the
     * guest thread between the title's frames, so nothing races the game. */
    if (manhunt_devmenu_combo(&pad)) {
        const uint32_t open = MEM32(DEVMENU_OPEN) ? 0u : 1u;
        MEM32(DEVMENU_OPEN) = open;
        printf("[MH-DEVMENU] %s\n", open ? "opened" : "closed");
    }
#ifdef MANHUNT_HORIZON
    {
        /* MANHUNT_FONT_FIX=1 (default off). The menu's big "lcd" font lives in
         * slot 0x003D172C, written only by 0x0014B320 when a front-end page
         * loads. Its dictionary is destroyed when a level starts, so on the
         * in-game settings page the slot points at a dead texture with no
         * raster and each letter is drawn as a solid box (runs 44 to 48).
         * Holding one more reference while the texture is valid keeps it alive:
         * +0x54 is the RwTexture reference count (0x001E9910 increments it,
         * 0x001E9760 releases it). At most 8 textures are held, about 256 KB
         * each. */
        static int fix = -1;
        static uint32_t held_tex, held_n;
        uint32_t t;
        int ok;
        if (fix < 0) {
            const char *e = getenv("MANHUNT_FONT_FIX");
            fix = e && *e == '1';
        }
        t = MEM32(0x003D172Cu);
        ok = (t >= 0x10000u && t < 0x04000000u) || (t >= 0x80000000u && t < 0x84000000u);
        if (fix && ok && t != held_tex && held_n < 8 && MEM32(t) != 0) {
            MEM32(t + 0x54) += 1;
            held_tex = t;
            held_n++;
            printf("[MH-FONT] holding slot 2 texture 0x%08X raster 0x%08X refs %u\n",
                   t, MEM32(t), MEM32(t + 0x54));
        }
    }
    {
        /* Timeline of the font slot the menu pages load (0x0014B320 writes it). */
        static uint32_t last, n;
        uint32_t t = MEM32(0x003D172Cu);
        if (t != last && n < 60 && getenv("MANHUNT_PAGE_CAPTURE")) {
            int ok = (t >= 0x10000u && t < 0x04000000u) || (t >= 0x80000000u && t < 0x84000000u);
            char name[33] = {0};
            uint32_t c;
            if (ok)
                for (c = 0; c < 32; c++) {
                    name[c] = (char)MEM8(t + 0x10 + c);
                    if (!name[c]) break;
                }
            n++;
            printf("[MH-SLOT2] %08X -> %08X raster %08X name \"%s\" page %d mode %u dict %08X\n", last, t,
                   ok ? MEM32(t) : 0, name, (int)MEM32(0x002EC5B8u), MEM32(0x003D8354u), MEM32(0x003D8CE8u));
            last = t;
        }
    }
    {
        /* MANHUNT_PAGE_CAPTURE: ZL+ZR logs every draw of the next frame. */
        extern volatile int nv2a_gl_page_request;
        static int both;
        int now = pad.analog[MH_LT] && pad.analog[MH_RT];
        if (now && !both) {
            nv2a_gl_page_request = 1;
            printf("[MH-REQ] requested\n");
            {
                /* The text code's font texture slots (0x00145846..0x00145868):
                 * 0 "manhunt_xbox", 1 "console", 2 set by the menu code,
                 * 3 "loading". A slot holds an RwTexture: raster at +0,
                 * name at +0x10. */
                uint32_t k, idx = MEM32(0x003D7D5Cu);
                printf("[MH-FONT] select %u -> font %u, current %u, override 0x%08X\n", idx,
                       idx < 8 ? MEM32(0x003D7018u + idx * 4) : 0xFFFFFFFFu,
                       MEM32(0x003D6FD4u), MEM32(0x003D7D40u));
                for (k = 0; k < 4; k++) {
                    uint32_t t = MEM32(0x003D1724u + k * 4);
                    int ok = (t >= 0x10000u && t < 0x04000000u) || (t >= 0x80000000u && t < 0x84000000u);
                    char name[33] = {0};
                    uint32_t c;
                    if (ok)
                        for (c = 0; c < 32; c++) {
                            name[c] = (char)MEM8(t + 0x10 + c);
                            if (!name[c]) break;
                        }
                    printf("[MH-FONT] slot %u texture 0x%08X raster 0x%08X name \"%s\"\n", k, t,
                           ok ? MEM32(t) : 0, name);
                }
            }
        }
        both = now;
    }
#endif
    {
        /* MANHUNT_MOVIE_SKIP=1 (default off): A or + newly pressed during a
         * movie ends it. 0x0039C8D4 counts while a movie plays (0x000E8B62, run 44:
         * 134 during one, 0x0039C8D1 stayed 0); 0x0039C8D3 is the end-of-movie byte the
         * movie code sets itself (0x000E8ED4, 0x000E8F52) and the per-frame
         * routine 0x000E99B0 acts on at 0x000E9BEA, so a skip takes the same
         * path as a movie that finished. */
        static int skip = -1, held;
        if (skip < 0) {
            const char *e = getenv("MANHUNT_MOVIE_SKIP");
            skip = e && *e == '1';
        }
        if (skip) {
            int down = pad.analog[MH_A] || (pad.buttons & MH_PAD_START);
            static unsigned presses;
            if (down && !held && presses < 40) {
                presses++;
                printf("[MH-SKIP] press: d1=%u d3=%u c8=%u d4=%u\n", MEM8(0x0039C8D1u),
                       MEM8(0x0039C8D3u), MEM32(0x0039C8C8u), MEM32(0x0039C8D4u));
            }
            if (down && !held && MEM32(0x0039C8D4u)) {
                MEM8(0x0039C8D3u) = 1;
                printf("[MH-SKIP] movie ended by button\n");
            }
            held = down;
        }
    }
    {
        /* Evidence for hardware runs: how often the title polls and what it is
         * handed. First polls, every change, and a heartbeat every 50th poll. */
        static uint32_t polls, logged;
        static uint16_t last_btn;
        static unsigned last_an;
        unsigned an = 0;
        for (i = 0; i < 8; i++)
            if (pad.analog[i]) an |= 1u << i;
        polls++;
        if (logged < 400 && (polls <= 5 || pad.buttons != last_btn || an != last_an || polls % 50 == 0)) {
            logged++;
            printf("[MH-INPUT] poll #%u delivered buttons=0x%04x analog=0x%02x\n", polls, pad.buttons, an);
        }
        last_btn = pad.buttons;
        last_an = an;
    }
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
