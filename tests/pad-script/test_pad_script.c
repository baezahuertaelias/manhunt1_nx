/* Focused test for MANHUNT_PAD_SCRIPT stick directions (host, no game data).
 * Build: cc -I game/src -o t tests/pad-script/test_pad_script.c game/src/pad_script.c */
#include <stdio.h>
#include <stdlib.h>
#include "pad.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    ManhuntPad p;
    /* Active immediately and for a long hold; one later step never active. */
    setenv("MANHUNT_PAD_SCRIPT",
           "0:lup+rleft+a:600000,0:lleft+lright:600000,0:up:600000,9000000:ldown", 1);
    CHECK(manhunt_pad_script(&p) == 1);
    CHECK(p.ly == 32767);          /* left stick up */
    CHECK(p.lx == 0);              /* lleft+lright cancel; idle axis stays 0 */
    CHECK(p.rx == -32768);         /* right stick left */
    CHECK(p.ry == 0);
    CHECK(p.analog[MH_A] == 255);
    CHECK(p.buttons == MH_PAD_UP);
    printf(fails ? "pad-script: %d failure(s)\n" : "pad-script: all checks passed\n", fails);
    return fails != 0;
}
