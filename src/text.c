//-----------------------------------------------------------------------------
// text.c — Layer 1 (8x8 text layer) rendering for x16-hero (Apple II VERA).
//
// Text map lives at $0000 (L1_MAP_ADDR).  Layer 1 is configured as 64 cols x
// 32 rows, 8x8 (config=0x10).  Each cell is 2 bytes (char screen code +
// color), so a row stride is 64*2 = 128 bytes.
//-----------------------------------------------------------------------------
#include <stdint.h>
#include "apple2e.h"
#include "text.h"

#define L1_MAP_ADDR 0x0000

uint8_t text_ascii_to_screen(uint8_t c) {
    /* Map lowercase to uppercase glyphs (uppercase zone 0x01-0x1A is proven
     * to hold the Latin letters in FONT.BIN; lowercase glyphs are absent). */
    if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
    if (c < 32) return c;
    if (c < 96) return c & 0x3F;       /* 'A'=0x41 -> 1 */
    if (c < 128) return c & 0x5F;
    return c;
}

void text_putc(uint8_t row, uint8_t col, uint8_t ch, uint8_t color) {
    uint16_t addr = L1_MAP_ADDR + (uint16_t)row * 128 + (uint16_t)col * 2;
    vera_set_addr(VERA_INC_BANK0, addr);
    VERA.data0 = text_ascii_to_screen(ch);
    VERA.data0 = color;
}

void text_putc_raw(uint8_t row, uint8_t col, uint8_t screenCode, uint8_t color) {
    uint16_t addr = L1_MAP_ADDR + (uint16_t)row * 128 + (uint16_t)col * 2;
    vera_set_addr(VERA_INC_BANK0, addr);
    VERA.data0 = screenCode;
    VERA.data0 = color;
}

void text_print(uint8_t row, uint8_t col, const char *s, uint8_t color) {
    while (*s) { text_putc(row, col++, *s++, color); }
}

void text_clear(void) {
    for (uint8_t r = 0; r < TEXT_ROWS; r++) {
        vera_set_addr(VERA_INC_BANK0, L1_MAP_ADDR + (uint16_t)r * 128);
        for (uint8_t c = 0; c < TEXT_COLS; c++) { VERA.data0 = 0x20; VERA.data0 = 0x01; }
    }
}

void text_bigfont(uint8_t row, uint8_t col, const uint8_t *pairs, uint8_t color) {
    while (*pairs) {
        uint8_t top = *pairs++;
        uint8_t bot = *pairs++;
        text_putc_raw(row,     col, top, color);
        text_putc_raw(row + 1, col, bot, color);
        col++;
    }
}
