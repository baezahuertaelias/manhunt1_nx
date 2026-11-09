/* The title's display gamma ramp (D3DDevice_SetGammaRamp), for the Switch presenters. The Xbox applies it at scanout
 * through the VGA DAC palette; here the guest's writes land in plain memory, so the presenters apply it themselves. */
#ifndef XBOX_GAMMA_H
#define XBOX_GAMMA_H

#include <stdint.h>

/* Fills r, g, b with the ramp the title last set. Returns 1 when there is one to apply: set, readable and not the
 * identity. 0 leaves the picture as it is, including with MANHUNT_GAMMA=0. Logs each new ramp once. */
int xbox_gamma_ramp(uint8_t r[256], uint8_t g[256], uint8_t b[256]);

#endif
