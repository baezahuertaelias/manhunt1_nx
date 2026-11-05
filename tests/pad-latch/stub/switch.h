/* Host stand-in for the parts of libnx that game/src/pad_switch.c uses. Test only. */
#ifndef STUB_SWITCH_H
#define STUB_SWITCH_H
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
typedef uint64_t u64;
typedef struct { int32_t x, y; } HidAnalogStickState;
typedef struct { int dummy; } PadState;
typedef pthread_mutex_t Mutex;
enum { HidNpadStyleSet_NpadStandard = 1 };
#define HID_BIT(n) (1ull << (n))
enum {
    HidNpadButton_A = HID_BIT(0), HidNpadButton_B = HID_BIT(1), HidNpadButton_X = HID_BIT(2), HidNpadButton_Y = HID_BIT(3),
    HidNpadButton_StickL = HID_BIT(4), HidNpadButton_StickR = HID_BIT(5), HidNpadButton_L = HID_BIT(6), HidNpadButton_R = HID_BIT(7),
    HidNpadButton_ZL = HID_BIT(8), HidNpadButton_ZR = HID_BIT(9), HidNpadButton_Plus = HID_BIT(10), HidNpadButton_Minus = HID_BIT(11),
    HidNpadButton_Left = HID_BIT(12), HidNpadButton_Up = HID_BIT(13), HidNpadButton_Right = HID_BIT(14), HidNpadButton_Down = HID_BIT(15)
};
/* test control: the "controller" */
extern volatile u64 g_test_buttons;
static inline void mutexInit(Mutex *m) { pthread_mutex_init(m, NULL); }
static inline void mutexLock(Mutex *m) { pthread_mutex_lock(m); }
static inline void mutexUnlock(Mutex *m) { pthread_mutex_unlock(m); }
static inline void svcSleepThread(int64_t ns) { usleep((useconds_t)(ns / 1000)); }
static inline void padConfigureInput(int n, int style) { (void)n; (void)style; }
static inline void padInitializeDefault(PadState *p) { (void)p; }
static inline void padUpdate(PadState *p) { (void)p; }
static inline u64 padGetButtons(PadState *p) { (void)p; return g_test_buttons; }
static inline HidAnalogStickState padGetStickPos(PadState *p, int i) { (void)p; (void)i; HidAnalogStickState s = {0, 0}; return s; }
static inline int padIsConnected(PadState *p) { (void)p; return 1; }
static inline int padIsHandheld(PadState *p) { (void)p; return 1; }
#endif
