/*
 * DirectSound at the API level: "no audio driver" for the boot milestone.
 *
 * Why: the statically linked XDK DirectSound programs the MCPX APU through
 * registers. There is no APU behind them here (no fault-trapped MMIO on
 * x86-64 POSIX, and Horizon/Eden does not fault at all), so the lifted
 * DirectSoundCreate "succeeded" against an empty aperture and
 * XAudioDownloadEffectsImage then wrote through a near-null object into the
 * game thread's TIB (reports/FEASIBILITY.md, step 8).
 *
 * Behaviour: the three creation entry points report DSERR_NODRIVER, as on a
 * console whose audio device is missing. Both callers handle it: the game's
 * Miles driver set-up (0x000143C0) logs and returns failure, and the XMV
 * player plays the intro without its audio stream. Audio is NOT implemented;
 * it is a milestone-6 item (docs/ROADMAP.md). Every other DSOUND entry point
 * is unreachable without an object; reaching one with a null object faults
 * loudly instead of corrupting memory.
 *
 * Addresses (.XTLID export IDs, XDK 5849): DirectSoundCreate 0x00225757
 * (ret 0xC), DirectSoundCreateStream 0x0022579E (ret 8),
 * XAudioDownloadEffectsImage 0x00223C31 (ret 0x10).
 */
#include <stdint.h>
#include <stdio.h>

#include "recomp_types.h"

#define DSERR_NODRIVER 0x88780078u

static uint32_t arg(int i) { return MEM32(g_esp + 4 + 4 * (uint32_t)i); }

static void fail_nodriver(const char *what, uint32_t out_ptr, uint32_t arg_bytes)
{
    static int logged;
    if (out_ptr)
        MEM32(out_ptr) = 0;
    if (logged++ < 8)
        printf("[MH-AUDIO] %s -> DSERR_NODRIVER (audio not implemented)\n", what);
    g_eax = DSERR_NODRIVER;
    g_esp += 4 + arg_bytes;
}

/* HRESULT DirectSoundCreate(LPGUID, LPDIRECTSOUND8 *ppDS, LPUNKNOWN) */
void sub_00225757(void) { fail_nodriver("DirectSoundCreate", arg(1), 12); }

/* HRESULT DirectSoundCreateStream(LPCDSSTREAMDESC, LPDIRECTSOUNDSTREAM *ppStream) */
void sub_0022579E(void) { fail_nodriver("DirectSoundCreateStream", arg(1), 8); }

/* HRESULT XAudioDownloadEffectsImage(LPCSTR, LPCDSEFFECTIMAGELOC, DWORD,
 *                                    LPDSEFFECTIMAGEDESC *ppImageDesc) */
void sub_00223C31(void) { fail_nodriver("XAudioDownloadEffectsImage", arg(3), 16); }
