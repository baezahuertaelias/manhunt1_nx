#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../game/src/nxfps_shared.h"

static void put_magic(uint8_t *b)
{
    uint32_t magic = NXFPS_MAGIC;
    memcpy(b, &magic, sizeof magic);
}

int main(void)
{
    uint8_t page[4096] = {0}, before[48], after[48];
    assert(manhunt_nxfps_first(page, sizeof page) == -1);
    put_magic(page + 0x16c);
    put_magic(page + 0xbc);
    assert(manhunt_nxfps_first(page, sizeof page) == 0xbc);
    manhunt_nxfps_timing(page + 0xbc, before);
    page[0xbc + NXFPS_ACTIVE] = 1;
    page[0xbc + 10] = 30; /* overlay-owned lock controls do not indicate a producer */
    manhunt_nxfps_timing(page + 0xbc, after);
    assert(!memcmp(before, after, sizeof before));
    page[0xbc + NXFPS_FRAME]++;
    manhunt_nxfps_timing(page + 0xbc, after);
    assert(memcmp(before, after, sizeof before));
    page[0xbc + NXFPS_FRAME]--;
    page[0xbc + NXFPS_TICKS + 39]++;
    manhunt_nxfps_timing(page + 0xbc, after);
    assert(memcmp(before, after, sizeof before));
    memset(page, 0, sizeof page);
    put_magic(page + 4092);
    assert(manhunt_nxfps_first(page, sizeof page) == -2);
    assert(manhunt_nxfps_first(page, 3) == -1);
    memset(page, 0, sizeof page);
    put_magic(page + 3920); /* last aligned complete block */
    assert(manhunt_nxfps_first(page, sizeof page) == 3920);
    puts("PASS Status Monitor: first-block selection, bounds, producer timing and reader-owned flags");
}
