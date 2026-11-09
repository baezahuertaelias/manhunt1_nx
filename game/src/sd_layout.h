/* SD deployment layout. Game assets remain separate from the application. */
#ifndef MANHUNT_SD_LAYOUT_H
#define MANHUNT_SD_LAYOUT_H
#ifndef MANHUNT_SD_BASE
#define MANHUNT_SD_BASE "sdmc:/switch/manhunt1_nx"
#endif
#define GAME_DIR   MANHUNT_SD_BASE "/game"
#define SAVE_DIR   MANHUNT_SD_BASE "/save"
#define FRAME_DIR  MANHUNT_SD_BASE "/frames"
#define ENV_FILE   MANHUNT_SD_BASE "/manhunt.env"
#endif
