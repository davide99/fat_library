#ifndef FAT_UTILS_H
#define FAT_UTILS_H

#include <stdint.h>
#include "fat_types.h"

uint32_t fat_log2(uint32_t x);
uint32_t fat_make_dword(uint16_t high, uint16_t low);

void fat_print_date(struct fat_date date);
void fat_print_time(struct fat_time time);
void fat_print_time_tenth(struct fat_time time, uint8_t tenth);
void fat_print_entry_attr(uint8_t attr);

#endif
