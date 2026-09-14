// input.c — Apple II keyboard mapped to the x16-hero _joy0 bit layout.
//
// The game logic reads _joy0 as a single byte (JOY_* bits).  We read the
// Apple II keyboard via $C000 (kbd helpers in apple2e.h) and translate keys
// into that layout so the game logic is unchanged from the X16 version.
#include <stdint.h>
#include "apple2e.h"
#include "input.h"

/* _joy0 bit layout (from joysticklib.asm). */
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

static uint8_t _joy0 = JOY_NOTHING_PRESSED;

/* ASCII / Apple II keyboard codes. */
#define K_UP    0x0B   /* cursor up    (11) */
#define K_DOWN  0x0A   /* cursor down  (10) */
#define K_LEFT  0x08   /* cursor left  (8)  */
#define K_RIGHT 0x15   /* cursor right (21) */

/* Keydown state (for decoupled edge and hold tracking). */
static uint8_t vertDir = 0;
static uint8_t vertHold = 0;
static uint8_t horizDir = 0;
static uint8_t horizHold = 0;
static uint8_t btnMask = 0;
static uint8_t btnHoldA = 0;
static uint8_t btnHoldB = 0;
static uint8_t btnHoldStart = 0;
static uint8_t lastKey = 0;
static uint8_t hasJoystick = 0;

#define JOY_DIR_HOLD_FRAMES 5
#define JOY_BTN_HOLD_FRAMES 3

/* Wait for a key, drain the keyboard buffer. */
void inputFlush(void) {
    while (kbd_pressed()) kbd_clear();
    vertDir = horizDir = btnMask = 0;
    vertHold = horizHold = btnHoldA = btnHoldB = btnHoldStart = 0;
    lastKey = 0;
    _joy0 = JOY_NOTHING_PRESSED;
}

/* Read one keypress, blocking.  Returns ASCII (or cursor code). */
uint8_t inputGetKey(void) {
    while (!kbd_pressed()) {}
    uint8_t k = kbd_read();
    kbd_clear();
    return k;
}

uint8_t inputKeyPressed(void) { return kbd_pressed() ? 1 : 0; }

uint8_t inputPollKey(void) {
    uint8_t k = lastKey;
    lastKey = 0;
    return k;
}

/* Apple II Native Paddle/Joystick Reading */
static uint8_t read_pdl(uint8_t pdl) {
    uint8_t count = 0;
#ifdef __mos__
    __asm__ volatile(
        "ldx %1\n\t"
        "lda 0xC070\n\t"
        "ldy #0\n\t"
        "nop\n\t"
        "nop\n"
        "1:\n\t"
        "lda 0xC064,x\n\t"
        "bpl 2f\n\t"
        "iny\n\t"
        "bne 1b\n\t"
        "dey\n"
        "2:\n\t"
        "sty %0"
        : "=r"(count)
        : "r"(pdl)
        : "a", "x", "y"
    );
#else
    (void)pdl;
#endif
    return count;
}

/* Sample keyboard and native joystick paddles each frame to build _joy0 (active-LOW). */
void inputUpdate(void) {
    if (kbd_pressed()) {
        uint8_t k = kbd_read() & 0x7F;
        kbd_clear();
        lastKey = k;

        /* Vertical axis: Up (11 / W), Down (10 / S) */
        if (k == K_UP || k == 'W' || k == 'w') {
            vertDir = JOY_UP; vertHold = JOY_DIR_HOLD_FRAMES;
        } else if (k == K_DOWN || k == 'S' || k == 's') {
            vertDir = JOY_DOWN; vertHold = JOY_DIR_HOLD_FRAMES;
        }

        /* Horizontal axis: Left (8 / A), Right (21 / D) */
        if (k == K_LEFT || k == 'A' || k == 'a') {
            horizDir = JOY_LEFT; horizHold = JOY_DIR_HOLD_FRAMES;
        } else if (k == K_RIGHT || k == 'D' || k == 'd') {
            horizDir = JOY_RIGHT; horizHold = JOY_DIR_HOLD_FRAMES;
        }

        /* Action buttons: Laser (X / Space), Dynamite (Z / J), Start (1 / Return) */
        if (k == 'X' || k == 'x' || k == ' ') {
            btnMask |= JOY_BUTTON_A; btnHoldA = JOY_BTN_HOLD_FRAMES;
        }
        if (k == 'Z' || k == 'z' || k == 'J' || k == 'j') {
            btnMask |= JOY_BUTTON_B; btnHoldB = 2;
        }
        if (k == '\r' || k == '1') {
            btnMask |= JOY_START; btnHoldStart = JOY_DIR_HOLD_FRAMES;
        }
    }

    /* Apple IIe Any-Key-Down ($C010 bit 7): eliminates keyboard auto-repeat delay!
     * When holding a key down, keep direction active continuously without pauses.
     * When the user releases the key, immediately cancel direction. */
    uint8_t anyKeyDown = ((*(volatile uint8_t *)0xC010 & 0x80) != 0);
    if (anyKeyDown) {
        if (vertDir) vertHold = JOY_DIR_HOLD_FRAMES;
        if (horizDir) horizHold = JOY_DIR_HOLD_FRAMES;
    } else {
        vertDir = 0; vertHold = 0;
        horizDir = 0; horizHold = 0;
    }

    if (btnHoldA > 0) { btnHoldA--; if (btnHoldA == 0) btnMask &= ~JOY_BUTTON_A; }
    if (btnHoldB > 0) { btnHoldB--; if (btnHoldB == 0) btnMask &= ~JOY_BUTTON_B; }
    if (btnHoldStart > 0) { btnHoldStart--; if (btnHoldStart == 0) btnMask &= ~JOY_START; }

    uint8_t activeMask = vertDir | horizDir | btnMask;

    /* Apple II Native Joystick Buttons:
     * Button 0 ($C061) = Laser (Button A)
     * Button 1 ($C062) & Button 2 ($C063) = Dynamite (Button B) */
    if ((*(volatile uint8_t *)0xC061 & 0x80) != 0) activeMask |= JOY_BUTTON_A;
    if ((*(volatile uint8_t *)0xC062 & 0x80) != 0) activeMask |= JOY_BUTTON_B;
    if ((*(volatile uint8_t *)0xC063 & 0x80) != 0) activeMask |= JOY_BUTTON_B;

    /* Apple II Native Joystick Paddles: Paddle 0 (X), Paddle 1 (Y).
     * Center is ~128. Thresholds 110 / 145 provide an immediate, crisp response
     * without requiring the stick to be pushed to the mechanical limits. */
    uint8_t pdlX = read_pdl(0);
    uint8_t pdlY = read_pdl(1);
    if ((pdlX > 10 || pdlY > 10) && (pdlX < 250 || pdlY < 250)) {
        if (pdlX < 110)      activeMask |= JOY_LEFT;
        else if (pdlX > 145) activeMask |= JOY_RIGHT;
        if (pdlY < 110)      activeMask |= JOY_UP;
        else if (pdlY > 145) activeMask |= JOY_DOWN;
    }

    _joy0 = (uint8_t)(JOY_NOTHING_PRESSED & ~activeMask);
}

uint8_t inputGetJoy(void) { return _joy0; }

void inputInit(void) {
    inputFlush();
}

