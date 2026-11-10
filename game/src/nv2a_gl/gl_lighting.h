#ifndef MANHUNT_GL_LIGHTING_H
#define MANHUNT_GL_LIGHTING_H
#include <stdint.h>
#include <string.h>

static inline void nv2a_gl_light_uniforms(const uint32_t *r, uint32_t lighting, float light[46][4])
{
    memset(light, 0, 46 * 4 * sizeof(float));
        if (lighting & 1u) {
            const uint32_t cm = r[0x0298 / 4];
            light[0][0] = 1;
            light[0][1] = (lighting & 4u) && ((cm >> 2) & 3) == 1;
            light[0][2] = (lighting & 4u) && ((cm >> 4) & 3) == 1;
            light[0][3] = !!(lighting & 2u);
            memcpy(light[1], r + 0x0A10 / 4, 12);
            memcpy(&light[1][3], r + 0x03B4 / 4, 4);
            memcpy(light[2], r + 0x0480 / 4, 3 * 16);
            for (int i = 0; i < 8; ++i) {
                const unsigned base = (0x1000 + i * 0x80) / 4, row = 6 + i * 5;
                memcpy(light[row], r + base, 12);
                light[row][3] = (float)((r[0x03BC / 4] >> (2 * i)) & 3);
                memcpy(light[row + 1], r + base + 0x0C / 4, 12);
                memcpy(&light[row + 1][3], r + base + 0x24 / 4, 4);
                memcpy(light[row + 2], r + base + 0x34 / 4, 12);
                memcpy(light[row + 3], r + base + 0x5C / 4, 12);
                memcpy(light[row + 4], r + base + 0x68 / 4, 12);
            }
        }
}
#endif
