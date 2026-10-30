#ifndef MANHUNT_NV2A_DEKO3D_H
#define MANHUNT_NV2A_DEKO3D_H
#include <stdint.h>
#include "nv2a_backend.h"

typedef struct {
    uint64_t draws, clears, rejected_draws, rejected_clears, rejected_transforms;
} ManhuntNv2aDeko3dStats;

/* Mount RomFS before initialization. Initialization does not register a backend. */
int manhunt_nv2a_deko3d_init(void);
const Nv2aBackend *manhunt_nv2a_deko3d_backend(void);
/* Snapshot on the executor thread, or after unregistering the backend. */
void manhunt_nv2a_deko3d_stats(ManhuntNv2aDeko3dStats *out);
void manhunt_nv2a_deko3d_shutdown(void);
#endif
