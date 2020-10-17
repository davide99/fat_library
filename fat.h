#ifndef FAT_H
#define FAT_H

#include <stdint.h>

typedef uint8_t *(*fat_read_bytes_func_t)(uint64_t address, uint32_t bytes);

enum fat_version {
  FAT16, FAT32
};

struct fat_drive {
  enum fat_version type;

  //Sizes
  uint8_t log_bytes_per_sector;
  uint8_t log_sectors_per_cluster;
  uint32_t fat_size_sectors;

  //Pointers
  uint32_t first_partition_sector; //AKA reserved region start, AKA lba begin in MBR
  uint32_t first_fat_sector;
  uint32_t first_root_dir_sector;
  uint32_t first_data_sector;

  //Function pointers
  fat_read_bytes_func_t read_bytes;
};

int fat_init(struct fat_drive *fat_drive, uint32_t sector_size, fat_read_bytes_func_t read_bytes_func);
void fat_print_dir(struct fat_drive *fat_drive, uint32_t cluster);
void fat_save_file(struct fat_drive *fat_drive, uint32_t cluster, uint32_t bytes);

#endif
