#include "fat.h"
#include "fat_types.h"
#include "fat_utils.h"

#ifdef FAT_DEBUG
#include <stdio.h>
#endif

//Private functions
static int get_partition_info(struct fat_drive *fat_drive);
static int read_BPB(struct fat_drive *fat_drive);
static uint32_t first_sector_of_cluster(struct fat_drive *fat_drive, uint32_t cluster);
static void print_entry_info(struct fat_entry entry);

int fat_init(struct fat_drive *fat_drive, uint32_t sector_size, fat_read_bytes_func_t read_bytes_func) {
	fat_drive->read_bytes = read_bytes_func;

	if (get_partition_info(fat_drive))
		goto error;

	//We store the log2(sector_size)
	fat_drive->log_sector_size = fat_log2(sector_size);

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
	struct fat_BPB *bpb;
	uint32_t root_dir_sectors, data_sectors, count_of_clusters, total_sectors, fat_size_sectors;

	if ((bpb = (struct fat_BPB *) fat_drive->read_bytes(
		fat_drive->lba_begin << fat_drive->log_sector_size,
		sizeof(struct fat_BPB)))==NULL)
		goto error;

	//Should be equal to the previous set size
	if (bpb->bytes_per_sector!=(1u << fat_drive->log_sector_size))
		goto error;

	//Parse the BPB: save just what we need
	fat_drive->log_sectors_per_cluster = fat_log2(bpb->sectors_per_cluster);

	fat_size_sectors = (!bpb->fat_size_sectors_16 ? bpb->ver_dep.v32.fat_size_sectors_32
												  : bpb->fat_size_sectors_16);

	//Determine fat version, fatgen pag. 14
	root_dir_sectors = bpb->root_entries_count << 5u; //Each entry is 32 bytes
	//Ceil division: from bytes to sectors
	root_dir_sectors = (root_dir_sectors + ((1u << fat_drive->log_sector_size) - 1)) >> fat_drive->log_sector_size;
	fat_drive->first_root_dir_sector =
		fat_drive->lba_begin + bpb->reserved_sectors_count + fat_size_sectors*bpb->number_of_fats;
	fat_drive->first_data_sector = fat_drive->first_root_dir_sector + root_dir_sectors;

	total_sectors = (!bpb->total_sectors_16 ? bpb->total_sectors_32 : bpb->total_sectors_16);

	data_sectors =
		total_sectors - (bpb->reserved_sectors_count + bpb->number_of_fats*fat_size_sectors + root_dir_sectors);

	count_of_clusters = data_sectors >> fat_drive->log_sectors_per_cluster;

	if (count_of_clusters < 4085) {
		goto error; //FAT12
	} else if (count_of_clusters < 65525) {
		fat_drive->fat_version = FAT16;
	} else {
		fat_drive->fat_version = FAT32;
		//On FAT32 first_root_dir_sector as previously calculated should be 0
		//It is actually stored in the fat version dependent part of the BPB
		if (fat_drive->first_root_dir_sector)
			goto error;
		fat_drive->first_root_dir_sector =
			fat_drive->lba_begin + (bpb->ver_dep.v32.root_cluster << fat_drive->log_sectors_per_cluster);
	}

	//Check the signature
	if (bpb->signature!=0xAA55u)
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

		print_entry_info(*fatEntry);
	}
}

static inline uint32_t first_sector_of_cluster(struct fat_drive *fat_drive, uint32_t cluster) {
	return ((cluster - 2) << fat_drive->log_sectors_per_cluster) + fat_drive->first_data_sector;
}

static void print_entry_info(struct fat_entry entry) {
	printf("  Modified: ");
	fat_print_date(entry.write.date);
	printf(" ");
	fat_print_time(entry.write.time);
	printf("    Start: %d    Size: %d\n",
		   fat_make_dword(entry.first_cluster_high, entry.first_cluster_low), entry.file_size_bytes);

	printf("  Created: ");
	fat_print_date(entry.creation.date);
	printf(" ");
	fat_print_time_tenth(entry.creation.time, entry.creation.time_tenth_of_secs);
	printf("\n");

	printf("  Last access: ");
	fat_print_date(entry.last_access_date);
	printf("\n");

	printf("  ");
	fat_print_entry_attr(entry.attr);
	printf("\n");
}