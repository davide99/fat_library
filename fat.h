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
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors_count;
    uint8_t number_of_fats;
    uint16_t root_entries_count;
    uint32_t total_sectors;
    uint32_t fat_size_sectors;
    uint32_t hidden_sectors;
    //Calculated
    enum fat_version fat_version;

    //Function pointers
    fat_read_bytes_func_t read_bytes;
};

int fat_init(struct fat_drive *fatDrive, uint32_t sectorSize, fat_read_bytes_func_t readBytesFunc);

#ifdef FAT_DEBUG
int test();
void fat_print_root_dir(struct fat_drive *fatDrive);
void fat_print_sub(struct fat_drive *fatDrive);
#endif

#endif
