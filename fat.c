#include "fat.h"
#include "fat_types.h"
#include <string.h>

#ifdef FAT_DEBUG
#include <stdio.h>
#endif

//Private functions
static int get_partition_info(struct fat_drive *fat_drive);
static int read_BPB(struct fat_drive *fat_drive);
static uint32_t first_sector_of_cluster(struct fat_drive *fat_drive, uint32_t cluster);

int fat_init(struct fat_drive *fat_drive, uint32_t sectorSize, fat_read_bytes_func_t read_bytes_func) {
	fat_drive->read_bytes = read_bytes_func;

	if (get_partition_info(fat_drive))
		goto error;

	//We store the log2(sectorSize)
	fat_drive->log_sector_size = 0;

	while (sectorSize >>= 1u)
		fat_drive->log_sector_size++;

	if (read_BPB(fat_drive))
		goto error;

	return 0;

error:
	return -1;
}

static inline int get_partition_info(struct fat_drive *fat_drive) {
	uint8_t *mbr;
	struct mbr_partition_entry *mbr_partition;

	if ((mbr = fat_drive->read_bytes(0, 512))==NULL)
		goto error;

	//Check the signature
	if (mbr[510]!=0x55u || mbr[511]!=0xAAu)
		goto error;

	//Get the first entry in the partition table
	mbr_partition = (struct mbr_partition_entry *) (mbr + 0x1BEu);

	if (mbr_partition->type!=0) {
		fat_drive->lba_begin = mbr_partition->lba_begin;
		return 0;
	}

error:
	return -1;
}

static inline int read_BPB(struct fat_drive *fat_drive) {
	uint8_t *bpb;
	struct fat_BS_and_BPB *s;
	uint32_t root_dir_sectors, data_sectors, count_of_clusters;

	if ((bpb = fat_drive->read_bytes(fat_drive->lba_begin << fat_drive->log_sector_size, 512))==NULL)
		goto error;

	s = (struct fat_BS_and_BPB *) bpb;

	//Should be equal to the previous set size
	if (s->bytes_per_sector!=(1u << fat_drive->log_sector_size))
		goto error;

	//Parse the BPB: save just what we need
	fat_drive->sectors_per_cluster = s->sectors_per_cluster;
	fat_drive->reserved_sectors_count = s->reserved_sectors_count;
	fat_drive->number_of_fats = s->number_of_fats;
	fat_drive->root_entries_count = s->root_entries_count;
	fat_drive->total_sectors = (s->total_sectors_16==0 ? s->total_sectors_32 : s->total_sectors_16);
	fat_drive->fat_size_sectors = (s->fat_size_sectors_16==0 ? s->ver_dep.v32.fat_size_sectors_32
															 : s->fat_size_sectors_16);
	fat_drive->hidden_sectors = s->hidden_sectors;

	//Determine fat version
	root_dir_sectors = fat_drive->root_entries_count << 5u; //Each entry is 32 bytes
	//Ceil division: from bytes to sectors
	root_dir_sectors = (root_dir_sectors + ((1u << fat_drive->log_sector_size) - 1)) >> fat_drive->log_sector_size;
	fat_drive->first_root_dir_sector =
		fat_drive->lba_begin + s->reserved_sectors_count + s->number_of_fats*fat_drive->fat_size_sectors;
	fat_drive->first_data_sector = fat_drive->first_root_dir_sector + root_dir_sectors;

	data_sectors = fat_drive->total_sectors -
		(fat_drive->reserved_sectors_count +
			fat_drive->number_of_fats*fat_drive->fat_size_sectors +
			root_dir_sectors);

	count_of_clusters = data_sectors/fat_drive->sectors_per_cluster;

	//fatgen pag. 15
	if (count_of_clusters < 4085) {
		goto error;
	} else if (count_of_clusters < 65525) {
		fat_drive->fat_version = FAT16;
	} else {
		fat_drive->fat_version = FAT32;
		fat_drive->first_root_dir_sector =
			fat_drive->lba_begin + s->ver_dep.v32.root_cluster*fat_drive->sectors_per_cluster;
	}

	//Check the signature
	if (bpb[510]!=0x55u || bpb[511]!=0xAAu)
		goto error;

	return 0;

error:
	return -1;
}

void fat_print_dir(struct fat_drive *fat_drive, uint32_t first_cluster) {
	uint16_t i;
	struct fat_entry *fatEntry;
	int exit = 0;
	uint32_t where;

	if (first_cluster==0) { //We want the root dir?
		where = fat_drive->first_root_dir_sector << fat_drive->log_sector_size;
	} else {
		where = first_sector_of_cluster(fat_drive, first_cluster) << fat_drive->log_sector_size;
	}

	for (i = 0; !exit; i++, where += sizeof(struct fat_entry)) {
		fatEntry = (struct fat_entry *) fat_drive->read_bytes(where, sizeof(struct fat_entry));

		switch (fatEntry->name.base[0]) {
			case 0xE5u: printf("Deleted file: [?%.7s.%.3s]\n", fatEntry->name.base + 1, fatEntry->name.ext);
				continue;
			case 0x00u: exit = 1;
				continue;
			case 0x05u: //KANJI
				printf("File starting with 0xE5: [%c%.7s.%.3s]\n", 0xE5, fatEntry->name.base + 1, fatEntry->name.ext);
				break;
			default: printf("File: [%.8s.%.3s]\n", fatEntry->name.base, fatEntry->name.ext);
		}

		printf("  Modified: %04d-%02d-%02d %02d:%02d.%02d    Start: [%08X]    Size: %d\n",
			   1980 + fatEntry->write.date.years_from_1980, fatEntry->write.date.month, fatEntry->write.date.day,
			   fatEntry->write.time.hours, fatEntry->write.time.mins, fatEntry->write.time.sec_gran_2*2,
			   fatEntry->first_cluster_low | (uint32_t) (fatEntry->first_cluster_high << 16u),
			   fatEntry->file_size_bytes);
		printf("  Created: %04d-%02d-%02d %02d:%02d:%02d.%02d\n",
			   1980 + fatEntry->creation.date.years_from_1980, fatEntry->creation.date.month,
			   fatEntry->creation.date.day, fatEntry->creation.time.hours, fatEntry->creation.time.mins,
			   fatEntry->creation.time.sec_gran_2*2 + fatEntry->creation.time_tenth_of_secs/100,
			   fatEntry->creation.time_tenth_of_secs%100
		);
		printf("  Last access: %04d-%02d-%02d\n",
			   1980 + fatEntry->last_access_date.years_from_1980, fatEntry->last_access_date.month,
			   fatEntry->last_access_date.day
		);

		printf("  Attributes: ro %d, hidden %d, system %d, volume_id %d, dir %d, archive %d, long name %d\n",
			   (fatEntry->attr & ATTR_READ_ONLY)!=0, (fatEntry->attr & ATTR_HIDDEN)!=0,
			   (fatEntry->attr & ATTR_SYSTEM)!=0,
			   (fatEntry->attr & ATTR_VOLUME_ID)!=0, (fatEntry->attr & ATTR_DIRECTORY)!=0,
			   (fatEntry->attr & ATTR_ARCHIVE)!=0,
			   (fatEntry->attr & ATTR_LONG_NAME)!=0);
	}
}

static inline uint32_t first_sector_of_cluster(struct fat_drive *fat_drive, uint32_t cluster) {
	return (cluster - 2)*fat_drive->sectors_per_cluster + fat_drive->first_data_sector;
}
