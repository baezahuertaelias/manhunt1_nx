/* The controller quit combo: the pure logic (game/src/quit_combo.h) and the
 * real poller thread in game/src/pad_switch.c against the libnx stand-in.
 * ZL + ZR + Plus + Minus held MANHUNT_QUIT_HOLD_MS ends the game; anything
 * less, or letting go early, must not. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "stub/switch.h"
#include "../../game/src/pad.h"
#include "../../game/src/quit_combo.h"

volatile u64 g_test_buttons;
int manhunt_pad_script(ManhuntPad *out) { (void)out; return 0; }
void manhunt_pad_sample(ManhuntPad *out);
static volatile int g_quit_calls;
void manhunt_quit_request(void) { g_quit_calls++; }

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("PASS: %s\n", msg); else { printf("FAIL: %s\n", msg); fails++; } } while (0)

static ManhuntPad pad(int start, int back, int lt, int rt)
{
    ManhuntPad p;
    memset(&p, 0, sizeof p);
    if (start) p.buttons |= MH_PAD_START;
    if (back) p.buttons |= MH_PAD_BACK;
    p.analog[MH_LT] = lt ? 255 : 0;
    p.analog[MH_RT] = rt ? 255 : 0;
    return p;
}

static void logic(void)
{
    int held = 0, fired = 0;
    ManhuntPad all = pad(1, 1, 1, 1);
    for (int i = 0; i < 374; i++) fired += manhunt_quit_combo_step(&all, &held, 4, 1500);
    CHECK(fired == 0, "logic: 1496 ms held does not fire");
    fired += manhunt_quit_combo_step(&all, &held, 4, 1500);
    CHECK(fired == 1, "logic: fires on the sample that reaches 1500 ms");
    for (int i = 0; i < 1000; i++) fired += manhunt_quit_combo_step(&all, &held, 4, 1500);
    CHECK(fired == 1, "logic: fires once per hold, not every sample");
    ManhuntPad none = pad(0, 0, 0, 0);
    manhunt_quit_combo_step(&none, &held, 4, 1500);
    CHECK(held == 0, "logic: letting go resets the counter");
    ManhuntPad miss[4] = { pad(0, 1, 1, 1), pad(1, 0, 1, 1), pad(1, 1, 0, 1), pad(1, 1, 1, 0) };
    for (int k = 0; k < 4; k++) {
        held = 0; fired = 0;
        for (int i = 0; i < 5000; i++) fired += manhunt_quit_combo_step(&miss[k], &held, 4, 1500);
        CHECK(fired == 0, "logic: three of the four buttons never fires");
    }
    held = 0; fired = 0;
    for (int i = 0; i < 300; i++) fired += manhunt_quit_combo_step(&all, &held, 4, 1500);
    manhunt_quit_combo_step(&none, &held, 4, 1500);
    for (int i = 0; i < 300; i++) fired += manhunt_quit_combo_step(&all, &held, 4, 1500);
    CHECK(fired == 0, "logic: two short holds do not add up");
    held = 0;
    CHECK(manhunt_quit_combo_step(&all, &held, 1000, 1500) == 0 && manhunt_quit_combo_step(&all, &held, 1000, 1500) == 1,
          "logic: big time steps (a starved poller) still count real time");
}

static void poller(void)
{
    ManhuntPad p;
    setenv("MANHUNT_PAD_LATCH", "1", 1);
    setenv("MANHUNT_QUIT_HOLD_MS", "600", 1);
    manhunt_pad_sample(&p);                                  /* starts the poller */
    g_test_buttons = HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Plus;   /* Minus missing */
    usleep(1000000);
    CHECK(g_quit_calls == 0, "poller: ZL+ZR+Plus without Minus for 1 s does not quit");
    g_test_buttons = HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Plus | HidNpadButton_Minus;
    usleep(300000);
    g_test_buttons = 0;
    usleep(100000);
    CHECK(g_quit_calls == 0, "poller: all four for 0.3 s then release does not quit");
    g_test_buttons = HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Plus | HidNpadButton_Minus;
    usleep(1200000);
    CHECK(g_quit_calls == 1, "poller: all four held past 600 ms quits, once");
    g_test_buttons = 0;
}

int main(void)
{
    logic();
    poller();
    printf("%s\n", fails ? "QUIT COMBO TEST FAILED" : "QUIT COMBO TEST OK");
    return fails ? 1 : 0;
}
