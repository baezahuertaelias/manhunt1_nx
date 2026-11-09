/* Rollback gate: original lifted DSOUND runs only with the MCPX model. */
#include <stdint.h>
#include <stdio.h>
#include "recomp_types.h"
#include "audio_mmio.h"
int manhunt_audio_guest_gate(uint32_t entry)
{
    unsigned out_arg, bytes;
    if (manhunt_audio_enabled()) return 0;
    switch (entry) {
    case 0x00225757: out_arg = 1; bytes = 12; break;
    case 0x0022579E: out_arg = 1; bytes = 8; break;
    case 0x00223C31: out_arg = 3; bytes = 16; break;
    default: return 0;
    }
    uint32_t out = MEM32(g_esp + 4 + 4 * out_arg);
    if (out) MEM32(out) = 0;
    static unsigned logged;
    if (logged++ < 8)
        printf("[MH-AUDIO] %08X -> DSERR_NODRIVER (MCPX disabled)\n", entry);
    g_eax = 0x88780078u;
    g_esp += 4 + bytes;
    return 1;
}
