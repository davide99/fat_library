#include "fat_utils.h"
#include "fat.h"
#include <string.h>

uint32_t fat_log2(uint32_t x) {
	uint32_t ret = 0;

	while (x >>= 1u)
		ret++;

	return ret;
}

inline uint32_t fat_make_dword(uint16_t high, uint16_t low) {
	return ((uint32_t) high << 16u) | low;
}

uint8_t fat_sfn_checksum(uint8_t *name) {
	int i;
	uint8_t sum = 0;

	for (i = 0; i < 11; i++)
		sum = (sum << 7u) + (sum >> 1u) + *name++;

	return sum;
}

inline char fat_ascii_to_upper(char c) {
	if (c >= 'a' && c <= 'z')
		return (char) (c - 0x20);

	return c;
}

int fat_entry_ascii_name_equals(struct fat_entry entry, const char *name, uint8_t name_len) {
	uint8_t i, j;
	char fat_name[sizeof(entry.name)];

	for (i = 0; name[i]!='.' && i < name_len; i++)
		fat_name[i] = fat_ascii_to_upper(name[i]);

	j = i + 1;
	for (; i < (uint8_t) sizeof(entry.name.splitted.base); i++)
		fat_name[i] = ' ';
	//j points at the next name char
	//i point at the next fat_name char

	for (; j < name_len; j++, i++)
		fat_name[i] = fat_ascii_to_upper(name[j]);

	for (; i < (uint8_t) sizeof(fat_name); i++)
		fat_name[i] = ' ';

	return !memcmp(entry.name.whole, fat_name, sizeof(fat_name));
}
