#ifndef FAT_H
#define FAT_H

#include <stdint.h>
#define FAT_INTERNAL_BUFFER_SIZE 32

typedef void *(*fat_read_bytes_func_t)(uint64_t address, uint32_t bytes, void *buffer);

enum fat_version {
  FAT16, FAT32
} __attribute__ ((packed));

/*
 * The structs are typedefed since the user is not meant to directly
 * write into them
 */

typedef struct {
  enum fat_version type;

  //Sizes
  uint8_t log_bytes_per_sector;
  uint8_t log_sectors_per_cluster;
  uint16_t entries_per_cluster;
  uint32_t cluster_size_bytes;

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
  uint8_t buffer[FAT_INTERNAL_BUFFER_SIZE];
} __attribute__ ((packed)) fat_drive;

typedef struct {
  //Coordinates
  uint32_t cluster;
  uint32_t in_cluster_byte_offset;

  //File size
  uint32_t size_bytes;
} fat_file;

typedef struct {
  uint32_t cluster;
} fat_dir;

typedef struct {
  uint8_t name[11];

  fat_file next_entry;
} fat_list_entry;

struct m_fat {
  int (*mount)(fat_drive *drive, uint32_t sector_size, fat_read_bytes_func_t read_bytes_func);

  //File related
  int (*file_open)(fat_drive *drive, const char *path, fat_file *file);
  int (*file_open_in_dir)(fat_drive *drive, fat_dir *dir, const char *filename, fat_file *file);
  uint32_t (*file_read)(fat_drive *drive, fat_file *file, void *buffer, uint32_t buffer_len);

  //Dir related
  void (*dir_get_root)(fat_dir *dir);
  int (*dir_change)(fat_drive *drive, fat_dir *dir, const char *dir_name);

  //Dir list related
  void (*list_make_empty_entry)(fat_list_entry *list_entry);
  int (*list_get_next_entry_in_dir)(fat_drive *drive, fat_dir *current_dir, fat_list_entry *list_entry);
};

extern const struct m_fat fat;

#endif
