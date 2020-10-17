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
  uint8_t log_bytes_per_sector;
  //From MBR
  uint32_t partition_start_sector;
  //From BPB
  uint8_t log_sectors_per_cluster;
  uint16_t reserved_sectors_count;
  uint32_t fat_size_sectors;
  //Calculated
  enum fat_version type;
  uint32_t first_data_sector;
  uint32_t first_root_dir_sector;

  //Function pointers
  fat_read_bytes_func_t read_bytes;
};

int fat_init(struct fat_drive *fat_drive, uint32_t sector_size, fat_read_bytes_func_t read_bytes_func);
void fat_print_dir(struct fat_drive *fat_drive, uint32_t cluster);
void fat_save_file(struct fat_drive *fat_drive, uint32_t cluster, uint32_t bytes);

#endif
