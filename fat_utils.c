#include "fat_utils.h"
#include <stdio.h>

uint32_t fat_log2(uint32_t x) {
	uint32_t ret = 0;

	while (x >>= 1u)
		ret++;

	return ret;
}

inline void fat_print_date(struct fat_date date) {
	printf("%02d/%02d/%04d",
		   date.day,
		   date.month,
		   FAT_BASE_YEAR + date.years_from_1980);
}

inline void fat_print_time(struct fat_time time) {
	printf("%02d:%02d:%02d",
		   time.hours,
		   time.mins,
		   time.sec_gran_2 << 1u);
}

inline void fat_print_time_tenth(struct fat_time time, uint8_t tenth) {
	printf("%02d:%02d:%02d.%02d",
		   time.hours,
		   time.mins,
		   (time.sec_gran_2 << 1u) + tenth/100,
		   tenth%100);
}

inline uint32_t fat_make_dword(uint16_t high, uint16_t low) {
	return ((uint32_t) high << 16u) | low;
}

inline void fat_print_entry_attr(uint8_t attr) {
	printf("Attributes: ro %d, hidden %d, system %d, volume_id %d, dir %d, archive %d, long name %d\n",
		   (attr & ATTR_READ_ONLY)!=0, (attr & ATTR_HIDDEN)!=0, (attr & ATTR_SYSTEM)!=0,
		   (attr & ATTR_VOLUME_ID)!=0, (attr & ATTR_DIRECTORY)!=0, (attr & ATTR_ARCHIVE)!=0,
		   (attr & ATTR_LONG_NAME)!=0);
}
