#include "devmenu.h"

#include <stdlib.h>

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MANHUNT_DEVMENU");
        on = e && *e == '1';
    }
    return on;
}

int manhunt_devmenu_combo(ManhuntPad *pad)
{
    static int held;
    const uint16_t both = MH_PAD_LTHUMB | MH_PAD_RTHUMB;
    if (!enabled()) return 0;
    if ((pad->buttons & both) != both) {
        held = 0;
        return 0;
    }
    pad->buttons &= (uint16_t)~both;
    if (held) return 0;
    held = 1;
    return 1;
}
