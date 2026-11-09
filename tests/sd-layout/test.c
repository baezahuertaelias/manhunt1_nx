#include "sd_layout.h"
#include <assert.h>
#include <string.h>
int main(void)
{
#ifdef EXPECT_OVERRIDE
    assert(!strcmp(GAME_DIR, "sdmc:/custom/game"));
    assert(!strcmp(ENV_FILE, "sdmc:/custom/manhunt.env"));
#else
    assert(!strcmp(GAME_DIR, "sdmc:/switch/manhunt1_nx/game"));
    assert(!strcmp(GAME_DIR "/DEFAULT.xbe", "sdmc:/switch/manhunt1_nx/game/DEFAULT.xbe"));
    assert(!strcmp(ENV_FILE, "sdmc:/switch/manhunt1_nx/manhunt.env"));
    assert(!strcmp(SAVE_DIR, "sdmc:/switch/manhunt1_nx/save"));
    assert(!strcmp(FRAME_DIR, "sdmc:/switch/manhunt1_nx/frames"));
#endif
    return 0;
}
