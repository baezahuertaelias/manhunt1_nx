# pad latch tests

Host tests (macOS or Linux, no console) that execute the real `game/src/pad_switch.c` against a stand-in for the few libnx calls it uses (`stub/switch.h`).

    cd tests/pad-latch
    clang -std=c11 -Wall -Wextra -I stub ../../game/src/pad_switch.c test_pad_latch.c -o /tmp/test_pad_latch -lpthread && /tmp/test_pad_latch
    TEST_HOLD=3 ...same with test_pad_hold.c...   # a tap is delivered on exactly N polls
    TEST_LATCH=0 /tmp/test_pad_latch              # the old behaviour must FAIL the tap checks

Scope: the latch and hold logic only. They say nothing about the kernel, libnx timing or the console.
