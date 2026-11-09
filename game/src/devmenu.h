/* Opens Rockstar's developer menu, which ships in the Xbox build with its opening input removed (reports/DEVMENU.md).
 * MANHUNT_DEVMENU=1: L3 and R3 pressed together toggle it. Off by default: the menu changes gameplay. */
#ifndef DEVMENU_H
#define DEVMENU_H

#include "pad.h"

/* Call once per game poll, on the polling guest thread, before the pad reaches the title. Returns 1 on the poll where
 * the combo is first held (toggle the menu then), and always removes L3 and R3 from *pad while both are held, so the
 * title never acts on them. Returns 0 and changes nothing with the switch off. */
int manhunt_devmenu_combo(ManhuntPad *pad);

#endif
