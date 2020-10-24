#ifndef FAT_UTILS_H
#define FAT_UTILS_H

#include <stdint.h>
#include "fat_types.h"

uint32_t fat_log2(uint32_t x);
uint32_t fat_make_dword(uint16_t high, uint16_t low);
uint8_t fat_sfn_checksum(uint8_t *name);

char fat_ascii_to_upper(char c);
int fat_entry_ascii_name_equals(struct fat_entry entry, const char *name);
int fat_split_path(const char *path, char *buffer, int *is_last);

#endif
