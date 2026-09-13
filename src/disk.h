#ifndef DISK_H
#define DISK_H
#include <stdint.h>

void disk_init(void);
/* bank: 0 = VRAM bank 0, 1 = VRAM bank 1.  vram_addr is the 16-bit part. */
void disk_copy_to_vram(uint32_t offset, uint32_t vram_addr, uint32_t length, uint8_t bank);
void disk_copy_to_ram(uint32_t offset, uint8_t *dest, uint32_t length);
void disk_read_hiscore(uint8_t *dest, uint16_t length);
uint8_t disk_write_hiscore(const uint8_t *src, uint16_t length);
void mlib_quit(void);

#endif
