#ifndef FAT_PRIV_DEFS_H
#define FAT_PRIV_DEFS_H

#include <stdint.h>

//Bootsector and BPB
struct fat_BPB {
  uint8_t padding1[11];
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_sectors_count;
  uint8_t number_of_fats;
  uint16_t root_entries_count;
  uint16_t total_sectors_16;
  uint8_t media;
  uint16_t fat_size_sectors_16;
  uint16_t sectors_per_track;
  uint16_t number_of_heads;
  uint32_t hidden_sectors;
  uint32_t total_sectors_32;
  union {
	struct {
	  uint8_t drive_number;
	  uint8_t reserved1;
	  uint8_t boot_signature;
	  uint32_t volume_id;
	  uint8_t volume_label[11];
	  uint8_t filesystem_type[8];
	} __attribute__((packed)) v16;
	struct {
	  uint32_t fat_size_sectors_32;
	  uint16_t extra_flags;
	  uint16_t filesystem_version;
	  uint32_t root_cluster;
	  uint16_t filesystem_info_sector;
	  uint16_t backup_boot_sector;
	  uint8_t reserved[12];
	  uint8_t drive_number;
	  uint8_t reserved1;
	  uint8_t boot_signature;
	  uint32_t volume_id;
	  uint8_t volume_label[11];
	  uint8_t filesystem_type[8];
	} __attribute__((packed)) v32;
  } __attribute__((packed)) ver_dep;
  uint8_t padding2[420];
  uint16_t signature;
} __attribute__((packed));

//FAT specific types
struct fat_date {
  uint16_t day: 5,
	  month: 4,
	  years_from_1980: 7;
} __attribute__((packed));

struct fat_time {
  uint16_t sec_gran_2: 5,
	  mins: 6,
	  hours: 5;
} __attribute__((packed));

struct fat_entry {
  struct {
	uint8_t base[8];
	uint8_t ext[3];
  } __attribute__((packed)) name;
  uint8_t attr;
  uint8_t reserved;
  struct {
	uint8_t time_tenth_of_secs;
	struct fat_time time;
	struct fat_date date;
  } __attribute__((packed)) creation;
  struct fat_date last_access_date;
  uint16_t first_cluster_high;
  struct {
	struct fat_time time;
	struct fat_date date;
  } __attribute__((packed)) write;
  uint16_t first_cluster_low;
  uint32_t file_size_bytes;
} __attribute__((packed));

struct mbr_partition_entry {
  uint8_t status;
  uint8_t padding1[3];
  uint8_t type;
  uint8_t padding2[3];
  uint32_t lba_begin;
  uint32_t sectors;
} __attribute__((packed));

//fat_entry attrib masks
#define ATTR_READ_ONLY ((uint8_t)(0x01u))
#define ATTR_HIDDEN ((uint8_t)(0x02u))
#define ATTR_SYSTEM ((uint8_t)(0x04u))
#define ATTR_VOLUME_ID ((uint8_t)(0x08u))
#define ATTR_DIRECTORY ((uint8_t)(0x10u))
#define ATTR_ARCHIVE ((uint8_t)(0x20u))
#define ATTR_LONG_NAME ((uint8_t)(0x0Fu)) //READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID

#define FAT_BASE_YEAR (1980)

#endif
