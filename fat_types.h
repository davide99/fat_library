#ifndef FAT_TYPES_H
#define FAT_TYPES_H

#include <stdint.h>

//BPB
#define BPB_BYTE_OFFSET__FROM_PARTITION_BEGIN (11) //sizeof(char jmp_boot[3] + char oem_name[8])
struct fat_BPB {
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_sectors_count;
  uint8_t number_of_fats;
  uint16_t root_entries_count;
  uint16_t total_sectors_16;
  uint8_t unused1;		//media
  uint16_t fat_size_sectors_16;
  uint8_t unused2[8];	//sizeof(uint16_t sectors_per_track + uint16_t number_of_heads + uint32_t hidden_sectors)
  uint32_t total_sectors_32;
} __attribute__((packed)); //25B

//FAT32-specific BPB offsets
#define BPB32_BYTE_OFFEST__FAT_SIZE_SECTORS_32 (BPB_BYTE_OFFSET__FROM_PARTITION_BEGIN + sizeof(struct fat_BPB) + 0)
#define BPB32_BYTE_OFFEST__ROOT_CLUSTER_32 (BPB_BYTE_OFFSET__FROM_PARTITION_BEGIN + sizeof(struct fat_BPB) + 4 + 2 + 2)

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

struct fat_lfn_entry {
  uint8_t order;
  uint16_t name1[5];
  uint8_t attr;
  uint8_t type;
  uint8_t checksum;
  uint16_t name2[6];
  uint8_t padding[2];
  uint16_t name3[2];
} __attribute__((packed));

struct mbr_partition_entry {
  uint8_t status;
  uint8_t padding1[3];
  uint8_t type;
  uint8_t padding2[3];
  uint32_t lba_begin;
  uint32_t sectors;
} __attribute__((packed));

#define MBR_BOOT_SIG ((uint16_t)(0xAA55u))

//fat_entry attrib masks
#define ATTR_READ_ONLY ((uint8_t)(0x01u))
#define ATTR_HIDDEN ((uint8_t)(0x02u))
#define ATTR_SYSTEM ((uint8_t)(0x04u))
#define ATTR_VOLUME_ID ((uint8_t)(0x08u))
#define ATTR_DIRECTORY ((uint8_t)(0x10u))
#define ATTR_ARCHIVE ((uint8_t)(0x20u))
#define ATTR_LONG_NAME_MASK ((uint8_t)(0xFFu)) //READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID | DIRECTORY |ARCHIVE
#define ATTR_LONG_NAME ((uint8_t)(0x0Fu)) //READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID

#define FAT_BASE_YEAR (1980)

#define CLUSTER_MASK_32 (0x0FFFFFFFu)
#define CLUSTER_EOF_16 (0xFFF8u)
#define CLUSTER_EOF_32 (0x0FFFFFF8u)

#define LAST_LONG_ENTRY (0x40u)

/*
 * fatgen pag. 29 says "Long names are limited to 255 char",
 * each lfn entry contains 13 char => ceil(255/13)=20.
 */
#define MAX_ORDER_LFS_ENTRIES (20)

#endif
