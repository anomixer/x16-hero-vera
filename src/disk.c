//-----------------------------------------------------------------------------
// disk.c — ProDOS MLI READ_BLOCK streaming for x16-hero (Apple II VERA port).
//
// All game assets are packed into a single assets.blob placed at a FIXED block
// range in the HDV (ASSET_START_BLOCK, kept in sync with tools/build_hdv.mjs).
// The game reads blocks directly via MLI READ_BLOCK (no pathname parsing).
// Adapted from TimePilot-IIvera src/disk.c.
//-----------------------------------------------------------------------------
#include <stdint.h>
#include "apple2e.h"
#include "disk.h"

extern uint8_t mli_unit;
extern uint8_t mli_buf_lo, mli_buf_hi;
extern uint8_t mli_blk_lo, mli_blk_hi;
extern uint8_t mli_status;
extern void mlib_read_block(void);

#define BLOCK_BYTES 512

/* Dedicated 512-byte streaming buffer at $0800 (Text Page 2, free). */
static uint8_t * const diskBuf = (uint8_t *)0x0800;
static uint16_t cached_abs_block = 0xFFFF;
static uint8_t  cached_unit = 0xFF;

static uint8_t  boot_unit = 0;
static uint8_t  is_floppy = 0;

/* Fixed asset region in the HDV (kept in sync with build_hdv.mjs). */
#define ASSET_START_BLOCK 900
static uint32_t asset_base_block = ASSET_START_BLOCK;

void disk_init(void) {
    boot_unit = *(volatile uint8_t *)0xBF30;

    /* Inspect volume size via Key Block of Volume Directory (block 2). */
    mli_unit = boot_unit;
    mli_buf_lo = (uint8_t)((uint32_t)(unsigned long)diskBuf);
    mli_buf_hi = (uint8_t)(((uint32_t)(unsigned long)diskBuf) >> 8);
    mli_blk_lo = 2;
    mli_blk_hi = 0;
    mlib_read_block();
    uint16_t total_blocks = (uint16_t)diskBuf[0x29] | ((uint16_t)diskBuf[0x2A] << 8);

    if (total_blocks <= 280) {
        is_floppy = 1;
        asset_base_block = ASSET_START_BLOCK;
    } else {
        is_floppy = 0;
        asset_base_block = ASSET_START_BLOCK;
    }

    cached_abs_block = 0xFFFF;
    cached_unit = 0xFF;
}

static uint8_t *disk_asset(uint32_t offset) {
    uint32_t abs = asset_base_block + offset / BLOCK_BYTES;
    uint8_t unit = boot_unit;

    if ((uint16_t)abs != cached_abs_block || unit != cached_unit) {
        mli_unit = unit;
        mli_buf_lo = (uint8_t)((uint32_t)(unsigned long)diskBuf);
        mli_buf_hi = (uint8_t)(((uint32_t)(unsigned long)diskBuf) >> 8);
        mli_blk_lo = (uint8_t)abs;
        mli_blk_hi = (uint8_t)(abs >> 8);
        mlib_read_block();
        cached_abs_block = (uint16_t)abs;
        cached_unit = unit;
    }
    return &diskBuf[offset & (BLOCK_BYTES - 1)];
}

/* Stream `length` bytes starting at asset `offset` into VRAM `vram_addr`.
 * bank: 0 = bank 0, 1 = bank 1.  vram_addr is the 16-bit part (bank bit is in
 * the address_hi increment value). */
void disk_copy_to_vram(uint32_t offset, uint32_t vram_addr, uint32_t length, uint8_t bank) {
    vera_set_addr(bank ? VERA_INC_BANK1 : VERA_INC_BANK0, (uint16_t)vram_addr);
    uint32_t off = 0;
    while (off < length) {
        uint8_t *chunk = disk_asset(offset + off);
        uint16_t in_blk = BLOCK_BYTES - (uint16_t)((offset + off) & (BLOCK_BYTES - 1));
        uint32_t rem = length - off;
        uint16_t n = (rem < in_blk) ? (uint16_t)rem : in_blk;
        for (uint16_t i = 0; i < n; i++) VERA.data0 = chunk[i];
        off += n;
    }
}

/* Read `length` bytes at asset `offset` into a RAM buffer at `dest`. */
void disk_copy_to_ram(uint32_t offset, uint8_t *dest, uint32_t length) {
    uint32_t off = 0;
    while (off < length) {
        uint8_t *chunk = disk_asset(offset + off);
        uint16_t in_blk = BLOCK_BYTES - (uint16_t)((offset + off) & (BLOCK_BYTES - 1));
        uint32_t rem = length - off;
        uint16_t n = (rem < in_blk) ? (uint16_t)rem : in_blk;
        for (uint16_t i = 0; i < n; i++) dest[off + i] = chunk[i];
        off += n;
    }
}
