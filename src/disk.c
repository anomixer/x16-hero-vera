// ProDOS file-based asset streaming and high-score persistence.
#include <stdint.h>
#include "apple2e.h"
#include "disk.h"
#include "asset_table.h"

extern uint8_t mli_status;
extern uint8_t mli_open_params[], mli_close_params[];
extern uint8_t mli_read_params[], mli_write_params[];
extern uint8_t mli_mark_params[], mli_create_params[], mli_eof_params[], mli_online_params[], mli_prefix_params[];
extern void mlib_open(void), mlib_close(void), mlib_read(void), mlib_write(void);
extern void mlib_set_mark(void), mlib_create(void), mlib_set_eof(void), mlib_on_line(void), mlib_get_prefix(void);

#define BLOCK_BYTES 512u
/* Reserve the OPEN buffer inside the linked application image so ProDOS cannot
 * mistake the fixed address for system-owned or unallocated memory. */
static uint8_t io_buffer[1024] __attribute__((aligned(256)));
static uint8_t transfer_buffer[BLOCK_BYTES] __attribute__((aligned(256)));
static uint8_t asset_ref;
static uint8_t asset_open;
static uint8_t file_path[65];
static uint8_t current_prefix[64];
static uint8_t current_prefix_length;
static uint8_t online_volumes[256] __attribute__((aligned(256)));

static const uint8_t assets_leaf[] = "ASSETS";
static const uint8_t score_leaf[] = "HISCORE.BIN";
static uint8_t open_file(const uint8_t *path, uint8_t *ref);
static uint8_t close_file(uint8_t ref);

static uint8_t make_full_path(const uint8_t *leaf) {
    uint8_t leaf_length = 0;
    while (leaf[leaf_length]) leaf_length++;
    uint8_t n = current_prefix_length;
    if ((uint16_t)n + leaf_length > 64) return 0;
    for (uint8_t i = 0; i < current_prefix_length; i++) file_path[i + 1] = current_prefix[i];
    for (uint8_t i = 0; i < leaf_length; i++) file_path[n + i + 1] = leaf[i];
    n += leaf_length;
    file_path[0] = n;
    return 1;
}

static uint8_t try_prefix_assets(void) {
    uintptr_t p = (uintptr_t)online_volumes;
    mli_prefix_params[1] = (uint8_t)p;
    mli_prefix_params[2] = (uint8_t)(p >> 8);
    mlib_get_prefix();
    if (mli_status || online_volumes[0] == 0 || online_volumes[0] > 63) return 0;
    current_prefix_length = online_volumes[0];
    for (uint8_t i = 0; i < current_prefix_length; i++) current_prefix[i] = online_volumes[i + 1];
    file_path[0] = 6;
    for (uint8_t i = 0; i < 6; i++) file_path[i + 1] = assets_leaf[i];
    if (!open_file(file_path, &asset_ref)) return 0;
    /* Keep the full current prefix so HISCORE.BIN follows the same directory. */
    if ((uint16_t)current_prefix_length + 11 <= 64) return 1;
    close_file(asset_ref);
    return 0;
}

static uint8_t open_file(const uint8_t *path, uint8_t *ref) {
    mli_open_params[1] = (uint8_t)(uintptr_t)path;
    mli_open_params[2] = (uint8_t)((uintptr_t)path >> 8);
    mli_open_params[3] = (uint8_t)(uintptr_t)io_buffer;
    mli_open_params[4] = (uint8_t)((uintptr_t)io_buffer >> 8);
    mli_open_params[5] = 0;
    mlib_open();
    if (mli_status) return 0;
    *ref = mli_open_params[5];
    return 1;
}

static uint8_t close_file(uint8_t ref) {
    mli_close_params[1] = ref;
    mlib_close();
    return mli_status == 0;
}

static uint8_t set_mark(uint8_t ref, uint32_t offset) {
    mli_mark_params[1] = ref;
    mli_mark_params[2] = (uint8_t)offset;
    mli_mark_params[3] = (uint8_t)(offset >> 8);
    mli_mark_params[4] = (uint8_t)(offset >> 16);
    mlib_set_mark();
    return mli_status == 0;
}

static uint8_t read_chunk(uint8_t ref, uint8_t *dst, uint16_t count, uint16_t *got) {
    mli_read_params[1] = ref;
    mli_read_params[2] = (uint8_t)(uintptr_t)dst;
    mli_read_params[3] = (uint8_t)((uintptr_t)dst >> 8);
    mli_read_params[4] = (uint8_t)count;
    mli_read_params[5] = (uint8_t)(count >> 8);
    mli_read_params[6] = mli_read_params[7] = 0;
    mlib_read();
    *got = (uint16_t)mli_read_params[6] | ((uint16_t)mli_read_params[7] << 8);
    return mli_status == 0 && *got == count;
}

static uint8_t write_chunk(uint8_t ref, const uint8_t *src, uint16_t count) {
    mli_write_params[1] = ref;
    mli_write_params[2] = (uint8_t)(uintptr_t)src;
    mli_write_params[3] = (uint8_t)((uintptr_t)src >> 8);
    mli_write_params[4] = (uint8_t)count;
    mli_write_params[5] = (uint8_t)(count >> 8);
    mli_write_params[6] = mli_write_params[7] = 0;
    mlib_write();
    return mli_status == 0 && mli_write_params[6] == (uint8_t)count && mli_write_params[7] == (uint8_t)(count >> 8);
}

static uint8_t create_score_file(void) {
    uintptr_t p = (uintptr_t)file_path;
    mli_create_params[1] = (uint8_t)p;
    mli_create_params[2] = (uint8_t)(p >> 8);
    mli_create_params[3] = 0xE3; /* readable, writable, destroyable, renameable */
    mli_create_params[4] = 0x06; /* binary */
    mli_create_params[5] = 0x00;
    mli_create_params[6] = 0x20;
    mli_create_params[7] = 0x01; /* standard file */
    mli_create_params[8] = mli_create_params[9] = 0;
    mli_create_params[10] = mli_create_params[11] = 0;
    mlib_create();
    return mli_status == 0;
}

void disk_init(void) {
    /* A2 DeskTop may launch MAIN.BIN from a disk other than ProDOS's boot
     * device. Try the active prefix first, then search mounted volume roots. */
    if (try_prefix_assets()) {
        asset_open = 1;
        return;
    }
    current_prefix_length = 0;
    mli_online_params[1] = 0;
    mli_online_params[2] = (uint8_t)(uintptr_t)online_volumes;
    mli_online_params[3] = (uint8_t)((uintptr_t)online_volumes >> 8);
    mlib_on_line();
    if (mli_status) return;
    for (uint16_t off = 0; off < 256; off += 16) {
        uint8_t name_len = online_volumes[off] & 0x0F;
        if (online_volumes[off] == 0) break;
        if (name_len == 0 || name_len > 15) continue;
        current_prefix_length = name_len + 2;
        current_prefix[0] = '/';
        for (uint8_t i = 0; i < name_len; i++) current_prefix[i + 1] = online_volumes[off + 1 + i] & 0x7F;
        current_prefix[name_len + 1] = '/';
        if (!make_full_path(assets_leaf)) continue;
        if (open_file(file_path, &asset_ref)) {
            asset_open = 1;
            break;
        }
    }
}

void disk_close_assets(void) {
    if (asset_open) {
        close_file(asset_ref);
        asset_open = 0;
    }
}

void disk_copy_to_vram(uint32_t offset, uint32_t vram_addr, uint32_t length, uint8_t bank) {
    vera_set_addr(bank ? VERA_INC_BANK1 : VERA_INC_BANK0, (uint16_t)vram_addr);
    while (length) {
        uint16_t n = length > BLOCK_BYTES ? BLOCK_BYTES : (uint16_t)length;
        if (!asset_open) return;
        if (!set_mark(asset_ref, offset)) return;
        uint16_t requested = n;
        if (!read_chunk(asset_ref, transfer_buffer, requested, &n)) return;
        for (uint16_t i = 0; i < n; i++) VERA.data0 = transfer_buffer[i];
        offset += n;
        length -= n;
    }
}

void disk_copy_to_ram(uint32_t offset, uint8_t *dest, uint32_t length) {
    while (length) {
        uint16_t n = length > BLOCK_BYTES ? BLOCK_BYTES : (uint16_t)length;
        if (!asset_open) return;
        if (!set_mark(asset_ref, offset)) return;
        uint16_t requested = n;
        if (!read_chunk(asset_ref, transfer_buffer, requested, &n)) return;
        for (uint16_t i = 0; i < n; i++) dest[i] = transfer_buffer[i];
        dest += n; offset += n; length -= n;
    }
}

void disk_read_hiscore(uint8_t *dest, uint16_t length) {
    uint8_t ref;
    for (uint16_t i = 0; i < length; i++) dest[i] = 0;
    if (current_prefix_length) {
        if (!make_full_path(score_leaf)) return;
    } else {
        file_path[0] = 11;
        for (uint8_t i = 0; i < 11; i++) file_path[i + 1] = score_leaf[i];
    }
    if (!open_file(file_path, &ref)) return;
    uint8_t ok = set_mark(ref, 0);
    uint16_t got = 0;
    if (ok) ok = read_chunk(ref, transfer_buffer, length, &got);
    if (ok) for (uint16_t i = 0; i < length; i++) dest[i] = transfer_buffer[i];
    close_file(ref);
}

uint8_t disk_write_hiscore(const uint8_t *src, uint16_t length) {
    uint8_t ref;
    if (current_prefix_length) {
        if (!make_full_path(score_leaf)) return 0x40;
    } else {
        file_path[0] = 11;
        for (uint8_t i = 0; i < 11; i++) file_path[i + 1] = score_leaf[i];
    }
    if (!open_file(file_path, &ref)) {
        if (!create_score_file() || !open_file(file_path, &ref)) return mli_status;
    }
    for (uint16_t i = 0; i < length; i++) transfer_buffer[i] = src[i];
    uint8_t ok = set_mark(ref, 0) && write_chunk(ref, transfer_buffer, length);
    if (ok) {
        mli_eof_params[1] = ref;
        mli_eof_params[2] = (uint8_t)length;
        mli_eof_params[3] = (uint8_t)(length >> 8);
        mli_eof_params[4] = 0;
        mlib_set_eof();
        ok = mli_status == 0;
    }
    uint8_t status = mli_status;
    uint8_t close_ok = close_file(ref);
    return (ok && close_ok) ? 0 : (status ? status : (mli_status ? mli_status : 0x27));
}
