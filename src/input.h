#ifndef INPUT_H
#define INPUT_H
#include <stdint.h>

/* _joy0 bit layout (shared with game logic). */
#define JOY_NOTHING_PRESSED 255
#define JOY_BUTTON_B  128
#define JOY_BUTTON_A   64
#define JOY_SELECT     32
#define JOY_START      16
#define JOY_UP          8
#define JOY_DOWN        4
#define JOY_LEFT        2
#define JOY_RIGHT       1
#define JOY_NO_DIRECTION 15

void inputInit(void);
void inputFlush(void);
void inputUpdate(void);      /* call each frame: builds _joy0 from keyboard */
uint8_t inputGetJoy(void);   /* current _joy0-style byte */
uint8_t inputKeyPressed(void);
uint8_t inputGetKey(void);   /* blocking key read (for text entry) */
uint8_t inputPollKey(void);  /* non-blocking: current keypress ASCII (0 if none), does not clear */

#endif
