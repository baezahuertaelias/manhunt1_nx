/* MANHUNT_PAD_HOLD_POLLS=N: a tap that happens between two game polls must be
 * delivered on exactly N consecutive polls, then disappear. Reads N from TEST_HOLD. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "stub/switch.h"
#include "../../game/src/pad.h"

volatile u64 g_test_buttons;
int manhunt_pad_script(ManhuntPad *out) { (void)out; return 0; }
void manhunt_pad_sample(ManhuntPad *out);

int main(void)
{
    int n = atoi(getenv("TEST_HOLD") ? getenv("TEST_HOLD") : "1"), fails = 0, seen = 0;
    ManhuntPad p;
    char hold[8];
    snprintf(hold, sizeof hold, "%d", n);
    setenv("MANHUNT_PAD_HOLD_POLLS", hold, 1);
    manhunt_pad_sample(&p);                      /* starts the poller */
    usleep(200000);
    g_test_buttons = HidNpadButton_Down;         /* a 100 ms tap between polls */
    usleep(100000);
    g_test_buttons = 0;
    usleep(200000);
    for (int poll = 1; poll <= n + 3; poll++) {
        manhunt_pad_sample(&p);
        int down = (p.buttons & MH_PAD_DOWN) != 0;
        int want = poll <= n;
        if (down) seen++;
        if (down != want) { printf("FAIL: N=%d poll %d: down=%d, expected %d\n", n, poll, down, want); fails++; }
        usleep(30000);
    }
    printf("N=%d: tap delivered on %d poll(s), expected %d: %s\n", n, seen, n, fails ? "FAIL" : "PASS");
    return fails != 0;
}
