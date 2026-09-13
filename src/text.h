#ifndef TEXT_H
#define TEXT_H
#include <stdint.h>

/* Layer 1 = 8x8 text (64 cols x 32 rows), map at $0000. */
#define TEXT_COLS 64
#define TEXT_ROWS 32

/* Convert ASCII to CX16 PETSCII *screen code* (FONT.BIN glyph index). */
uint8_t text_ascii_to_screen(uint8_t c);
/* Write one cell: char (ASCII converted) + color, at (row, col). */
void text_putc(uint8_t row, uint8_t col, uint8_t ch, uint8_t color);
/* Write a raw screen code (no ASCII conversion) — for big-font data. */
void text_putc_raw(uint8_t row, uint8_t col, uint8_t screenCode, uint8_t color);
/* Print a NUL-terminated ASCII string. */
void text_print(uint8_t row, uint8_t col, const char *s, uint8_t color);
/* Clear the whole text layer to spaces (white on transparent). */
void text_clear(void);
/* Draw a big 2-row font string: each element is a pair of screen codes
 * (top glyph, bottom glyph), 0 terminates.  Drawn at (row, col). */
void text_bigfont(uint8_t row, uint8_t col, const uint8_t *pairs, uint8_t color);

#endif
