#ifndef FAT_H
#define FAT_H

#include <stdint.h>
#define BUFFER_SIZE 32

typedef void *(*fat_read_bytes_func_t)(uint64_t address, uint32_t bytes, void *buffer);

enum fat_version {
  FAT16, FAT32
} __attribute__ ((packed));

struct fat_drive {
  enum fat_version type;

  //Sizes
  uint8_t log_bytes_per_sector;
  uint8_t log_sectors_per_cluster;
  uint16_t entries_per_cluster;

  //Pointers
  uint32_t first_partition_sector; //AKA reserved region start, AKA lba begin in MBR
  uint32_t first_fat_sector;
  union {
	uint32_t first_sector;
	uint32_t first_cluster;
  } root_dir;
  uint32_t first_data_sector;

  //Function pointers
  fat_read_bytes_func_t read_bytes;

  //Data
  uint8_t buffer[BUFFER_SIZE];
};

int fat_init(struct fat_drive *drive, uint32_t sector_size, fat_read_bytes_func_t read_bytes_func);
void fat_print_dir(struct fat_drive *drive, uint32_t cluster);
void fat_save_file(struct fat_drive *drive, uint32_t cluster, uint32_t size_bytes, void *buffer, uint16_t buffer_len);

#define ROOT_DIR_CLUSTER 0

#endif
