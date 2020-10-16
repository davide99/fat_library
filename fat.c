#include "fat.h"
#include <string.h>

#ifdef FAT_DEBUG

#include <stdio.h>

#endif

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

//Bootsector and BPB
struct fat_BS_and_BPB {
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
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

#define ATTR_READ_ONLY ((uint8_t)(0x01u))
#define ATTR_HIDDEN ((uint8_t)(0x02u))
#define ATTR_SYSTEM ((uint8_t)(0x04u))
#define ATTR_VOLUME_ID ((uint8_t)(0x08u))
#define ATTR_DIRECTORY ((uint8_t)(0x10u))
#define ATTR_ARCHIVE ((uint8_t)(0x20u))
#define ATTR_LONG_NAME ((uint8_t)(0x0Fu)) //READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID


static inline int get_partition_info(struct fat_drive *fatDrive) {
    uint8_t *ptr;
    struct mbr_partition_entry *mbr_partition;

    if ((ptr = fatDrive->read_bytes(0, 512)) == NULL)
        return -1;

    //Check the signature
    if (ptr[510] != 0x55u || ptr[511] != 0xAAu)
        return -1;

    //Get the first entry in the partition table
    mbr_partition = (struct mbr_partition_entry *) (ptr + 0x1BEu);

    if (mbr_partition->type != 0) {
        fatDrive->lba_begin = mbr_partition->lba_begin;
        return 0;
    }

    return -1;
}

static inline int read_BPB(struct fat_drive *fatDrive) {
    uint8_t *ptr;
    struct fat_BS_and_BPB *s;
    uint32_t root_dir_sectors, data_sectors, count_of_clusters;

    if ((ptr = fatDrive->read_bytes(fatDrive->lba_begin << fatDrive->log_sector_size, 512)) == NULL)
        goto error;

    s = (struct fat_BS_and_BPB *) ptr;

    //Parse the BPB
    //Should be equal to the previous set size
    if (s->bytes_per_sector != (1u << fatDrive->log_sector_size))
        goto error;

    fatDrive->sectors_per_cluster = s->sectors_per_cluster;
    fatDrive->reserved_sectors_count = s->reserved_sectors_count;
    fatDrive->number_of_fats = s->number_of_fats;
    fatDrive->root_entries_count = s->root_entries_count;
    fatDrive->total_sectors = (s->total_sectors_16 == 0 ? s->total_sectors_32 : s->total_sectors_16);
    fatDrive->fat_size_sectors = (s->fat_size_sectors_16 == 0 ? s->ver_dep.v32.fat_size_sectors_32
                                                              : s->fat_size_sectors_16);
    fatDrive->hidden_sectors = s->hidden_sectors;

    //Determine fat version
    root_dir_sectors = ((fatDrive->root_entries_count << 5u) + ((1u << fatDrive->log_sector_size) - 1))
            >> fatDrive->log_sector_size;

    data_sectors = fatDrive->total_sectors -
                   (fatDrive->reserved_sectors_count + (fatDrive->number_of_fats * fatDrive->fat_size_sectors) +
                    root_dir_sectors);

    count_of_clusters = data_sectors / fatDrive->sectors_per_cluster;

    if (count_of_clusters < 4085) {
        goto error;
    } else if (count_of_clusters < 65525) {
        fatDrive->fat_version = FAT16;
    } else {
        fatDrive->fat_version = FAT32;
    }

    //Check the signature
    if (ptr[510] != 0x55u || ptr[511] != 0xAAu)
        goto error;

    return 0;

    error:
    return -1;
}

int fat_init(struct fat_drive *fatDrive, uint32_t sectorSize, fat_read_bytes_func_t readBytesFunc) {
    fatDrive->read_bytes = readBytesFunc;

    if (get_partition_info(fatDrive))
        goto error;

    //We store the log2(sectorSize)
    fatDrive->log_sector_size = 0;

    while (sectorSize >>= 1u)
        fatDrive->log_sector_size++;

    if (read_BPB(fatDrive))
        goto error;

    return 0;

    error:
    return -1;
}

#ifdef FAT_DEBUG

void fat_print_root_dir(struct fat_drive *fatDrive) {
    uint16_t i;
    struct fat_entry *fatEntry;

    for (i = 0; i < fatDrive->root_entries_count; i++) {
        fatEntry = (struct fat_entry *) fatDrive->read_bytes(
                ((fatDrive->lba_begin + fatDrive->reserved_sectors_count +
                  fatDrive->fat_size_sectors * fatDrive->number_of_fats) << fatDrive->log_sector_size) +
                (i * sizeof(struct fat_entry)),
                sizeof(struct fat_entry)
        );

        switch (fatEntry->name.base[0]) {
            case 0xE5u:
                printf("Deleted file: [?%.7s.%.3s]\n", fatEntry->name.base + 1, fatEntry->name.ext);
                continue;
            case 0x00u:
                i = fatDrive->root_entries_count;
                continue;
            case 0x05u: //KANJI
                printf("File starting with 0xE5: [%c%.7s.%.3s]\n", 0xE5, fatEntry->name.base + 1, fatEntry->name.ext);
                break;
            default:
                printf("File: [%.8s.%.3s]\n", fatEntry->name.base, fatEntry->name.ext);
        }

        printf("  Modified: %04d-%02d-%02d %02d:%02d.%02d    Start: [%08X]    Size: %d\n",
               1980 + fatEntry->write.date.years_from_1980, fatEntry->write.date.month, fatEntry->write.date.day,
               fatEntry->write.time.hours, fatEntry->write.time.mins, fatEntry->write.time.sec_gran_2 * 2,
               fatEntry->first_cluster_low | (uint32_t) (fatEntry->first_cluster_high << 16u),
               fatEntry->file_size_bytes);
        printf("  Created: %04d-%02d-%02d %02d:%02d:%02d.%02d\n",
               1980 + fatEntry->creation.date.years_from_1980, fatEntry->creation.date.month,
               fatEntry->creation.date.day, fatEntry->creation.time.hours, fatEntry->creation.time.mins,
               fatEntry->creation.time.sec_gran_2 * 2 + fatEntry->creation.time_tenth_of_secs / 100,
               fatEntry->creation.time_tenth_of_secs % 100
        );
        printf("  Last access: %04d-%02d-%02d\n",
               1980 + fatEntry->last_access_date.years_from_1980, fatEntry->last_access_date.month,
               fatEntry->last_access_date.day
        );

        printf("  Attributes: ro %d, hidden %d, system %d, volume_id %d, dir %d, archive %d, long name %d\n",
               (fatEntry->attr & ATTR_READ_ONLY) != 0, (fatEntry->attr & ATTR_HIDDEN) != 0,
               (fatEntry->attr & ATTR_SYSTEM) != 0,
               (fatEntry->attr & ATTR_VOLUME_ID) != 0, (fatEntry->attr & ATTR_DIRECTORY) != 0,
               (fatEntry->attr & ATTR_ARCHIVE) != 0,
               (fatEntry->attr & ATTR_LONG_NAME) != 0);
    }
}

void fat_print_sub(struct fat_drive *fatDrive) {
    uint16_t i;
    struct fat_entry *fatEntry;
    int exit = 0;

    uint32_t rootDirSectors = ((fatDrive->root_entries_count * 32) + ((1u << fatDrive->log_sector_size) - 1)) /
                              (1u << fatDrive->log_sector_size);

    for (i = 0; !exit; i++) {
        uint32_t where = ((fatDrive->lba_begin + fatDrive->reserved_sectors_count +
                           fatDrive->fat_size_sectors * fatDrive->number_of_fats + rootDirSectors +
                           13 * fatDrive->sectors_per_cluster) << fatDrive->log_sector_size) +
                         (i * sizeof(struct fat_entry));

        fatEntry = (struct fat_entry *) fatDrive->read_bytes(where, sizeof(struct fat_entry));

        switch (fatEntry->name.base[0]) {
            case 0xE5u:
                printf("Deleted file: [?%.7s.%.3s]\n", fatEntry->name.base + 1, fatEntry->name.ext);
                continue;
            case 0x00u:
                exit = 1;
                continue;
            case 0x05u: //KANJI
                printf("File starting with 0xE5: [%c%.7s.%.3s]\n", 0xE5, fatEntry->name.base + 1, fatEntry->name.ext);
                break;
            default:
                printf("File: [%.8s.%.3s]\n", fatEntry->name.base, fatEntry->name.ext);
        }

        printf("  Modified: %04d-%02d-%02d %02d:%02d.%02d    Start: [%08X]    Size: %d\n",
               1980 + fatEntry->write.date.years_from_1980, fatEntry->write.date.month, fatEntry->write.date.day,
               fatEntry->write.time.hours, fatEntry->write.time.mins, fatEntry->write.time.sec_gran_2 * 2,
               fatEntry->first_cluster_low | (uint32_t) (fatEntry->first_cluster_high << 16u),
               fatEntry->file_size_bytes);
        printf("  Created: %04d-%02d-%02d %02d:%02d:%02d.%02d\n",
               1980 + fatEntry->creation.date.years_from_1980, fatEntry->creation.date.month,
               fatEntry->creation.date.day, fatEntry->creation.time.hours, fatEntry->creation.time.mins,
               fatEntry->creation.time.sec_gran_2 * 2 + fatEntry->creation.time_tenth_of_secs / 100,
               fatEntry->creation.time_tenth_of_secs % 100
        );
        printf("  Last access: %04d-%02d-%02d\n",
               1980 + fatEntry->last_access_date.years_from_1980, fatEntry->last_access_date.month,
               fatEntry->last_access_date.day
        );

        printf("  Attributes: ro %d, hidden %d, system %d, volume_id %d, dir %d, archive %d, long name %d\n",
               (fatEntry->attr & ATTR_READ_ONLY) != 0, (fatEntry->attr & ATTR_HIDDEN) != 0,
               (fatEntry->attr & ATTR_SYSTEM) != 0,
               (fatEntry->attr & ATTR_VOLUME_ID) != 0, (fatEntry->attr & ATTR_DIRECTORY) != 0,
               (fatEntry->attr & ATTR_ARCHIVE) != 0,
               (fatEntry->attr & ATTR_LONG_NAME) != 0);
    }
}

#endif