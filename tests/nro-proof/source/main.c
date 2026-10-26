#include <switch.h>
#include <stdio.h>

// Keep the console visible long enough for an emulator or hardware check,
// but exit on its own so headless runs terminate (~10 s at 60 Hz).
#define MAX_FRAMES 600

int main(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    printf("Manhunt Switch NRO proof\n");
    printf("Press + to exit.\n");

    for (int frame = 0; frame < MAX_FRAMES && appletMainLoop(); frame++) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }

    consoleExit(NULL);
    return 0;
}
