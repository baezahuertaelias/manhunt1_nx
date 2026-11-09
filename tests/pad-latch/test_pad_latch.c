/* Executes game/src/pad_switch.c against a stand-in libnx.
 * Scenario (the console's problem): the game polls once per ~1 s, a button is
 * pressed for only 100 ms between two polls. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "stub/switch.h"
#include "../../game/src/pad.h"

volatile u64 g_test_buttons;
int manhunt_pad_script(ManhuntPad *out) { (void)out; return 0; }
void manhunt_pad_sample(ManhuntPad *out);

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("PASS: %s\n", msg); else { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(void)
{
    ManhuntPad p;
    setenv("MANHUNT_PAD_LATCH", getenv("TEST_LATCH") ? getenv("TEST_LATCH") : "1", 1);

    manhunt_pad_sample(&p);                         /* poll 1: starts the poller, nothing pressed */
    CHECK(p.analog[MH_A] == 0 && p.buttons == 0, "poll 1: idle pad reads zero");

    usleep(300000);
    g_test_buttons = HidNpadButton_A | HidNpadButton_Down;  /* tap: 100 ms */
    usleep(100000);
    g_test_buttons = 0;
    usleep(600000);                                 /* the game polls only now, 1 s after poll 1 */

    manhunt_pad_sample(&p);                         /* poll 2 */
    CHECK(p.analog[MH_A] == 255 && p.analog[MH_B] == 0, "poll 2: the tap (Switch A = Xbox A) is delivered although released");
    CHECK((p.buttons & MH_PAD_DOWN) != 0, "poll 2: the d-pad tap is delivered");

    manhunt_pad_sample(&p);                         /* poll 3 right after */
    CHECK(p.analog[MH_A] == 0 && p.buttons == 0, "poll 3: a press is delivered once, not repeated");

    g_test_buttons = HidNpadButton_B;               /* held across polls */
    usleep(50000);
    manhunt_pad_sample(&p);
    CHECK(p.analog[MH_B] == 255, "held button is reported");
    usleep(50000);
    manhunt_pad_sample(&p);
    CHECK(p.analog[MH_B] == 255, "held button stays reported on the next poll");
    g_test_buttons = 0;
    usleep(50000);
    manhunt_pad_sample(&p);                         /* may still carry one latched sample */
    manhunt_pad_sample(&p);
    CHECK(p.analog[MH_B] == 0, "released button disappears");

    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", fails);
    return fails != 0;
}
