#ifndef FAT_H
#define FAT_H

#include <stdint.h>
//TODO: remove
#define FAT_DEBUG

typedef uint8_t *(*fat_read_bytes_func_t)(uint64_t address, uint32_t bytes);

enum fat_version {
  FAT16, FAT32
};

struct fat_drive {
  //From external driver
  uint16_t log_sector_size;
  //From MBR
  uint32_t lba_begin;
  //From BPB
  uint8_t log_sectors_per_cluster;
  //Calculated
  enum fat_version fat_version;
  uint32_t first_data_sector;
  uint32_t first_root_dir_sector;

  //Function pointers
  fat_read_bytes_func_t read_bytes;
};

int fat_init(struct fat_drive *fat_drive, uint32_t sector_size, fat_read_bytes_func_t read_bytes_func);
void fat_print_dir(struct fat_drive *fat_drive, uint32_t first_cluster);

#endif
