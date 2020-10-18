#include "fat.h"
#include "fat_types.h"
#include "fat_utils.h"
#include <stdio.h>

//Private functions
static int get_partition_info(struct fat_drive *drive);
static int read_BPB(struct fat_drive *drive);
static uint32_t first_sector_of_cluster(struct fat_drive drive, uint32_t cluster);
static void print_entry_info(struct fat_entry entry);
static uint32_t find_next_cluster(struct fat_drive fat, uint32_t current_cluster);
static int is_eof(struct fat_drive drive, uint32_t cluster);
static void print_lfn(struct fat_drive drive, struct fat_entry entry, uint64_t where);

int fat_init(struct fat_drive *drive, uint32_t sector_size, fat_read_bytes_func_t read_bytes_func) {
	drive->read_bytes = read_bytes_func;

	if (get_partition_info(drive))
		goto error;

	//We store the log2(sector_size)
	drive->log_bytes_per_sector = fat_log2(sector_size);

	if (read_BPB(drive))
		goto error;

	return 0;

error:
	return -1;
}

static inline int get_partition_info(struct fat_drive *drive) {
	struct mbr_partition_entry *mbr_partition;

	//Get the first entry in the partition table
	if ((mbr_partition = (struct mbr_partition_entry *) drive->read_bytes(
		0x1BEu,
		sizeof(struct mbr_partition_entry)))==NULL)
		goto error;

	if (mbr_partition->type!=0) {
		drive->first_partition_sector = mbr_partition->lba_begin;
		return 0;
	}

	//Check the signature
	if (*((uint16_t *) drive->read_bytes(510, 2))!=MBR_BOOT_SIG)
		goto error;

error:
	return -1;
}

static inline int read_BPB(struct fat_drive *drive) {
	struct fat_BPB *bpb;
	uint32_t root_dir_sectors, data_sectors_cluster;

	if ((bpb = (struct fat_BPB *) drive->read_bytes(
		drive->first_partition_sector << drive->log_bytes_per_sector,
		sizeof(struct fat_BPB)))==NULL)
		goto error;

	//Should be equal to the previous set size
	if (bpb->bytes_per_sector!=(1u << drive->log_bytes_per_sector))
		goto error;

	//Parse the BPB: save just what we need
	//Size
	drive->log_sectors_per_cluster = fat_log2(bpb->sectors_per_cluster);
	drive->fat_size_sectors = (!bpb->fat_size_sectors_16 ? bpb->ver_dep.v32.fat_size_sectors_32
														 : bpb->fat_size_sectors_16);
	drive->entries_per_cluster =
		(1u << (uint32_t) (drive->log_sectors_per_cluster + drive->log_bytes_per_sector))
			/sizeof(struct fat_entry);

	//Pointers
	drive->first_fat_sector = drive->first_partition_sector + bpb->reserved_sectors_count;
	drive->root_dir.first_sector = drive->first_fat_sector + drive->fat_size_sectors*bpb->number_of_fats;

	//Determine fat version, fatgen pag. 14, we need to be extra careful to avoid overflows
	//We end up with (at most) 16+5-9+1=13 bits. However total sector is 32 bit long
	root_dir_sectors =
		((bpb->root_entries_count << 5u) + ((1u << drive->log_bytes_per_sector) - 1)) >> drive->log_bytes_per_sector;

	drive->first_data_sector = drive->root_dir.first_sector + root_dir_sectors;

	data_sectors_cluster = ((!bpb->total_sectors_16 ? bpb->total_sectors_32 : bpb->total_sectors_16) -
		(drive->first_data_sector - drive->first_partition_sector)) >> drive->log_sectors_per_cluster;

	if (data_sectors_cluster < 4085) {
		goto error; //FAT12
	} else if (data_sectors_cluster < 65525) {
		drive->type = FAT16;
	} else {
		drive->type = FAT32;
		//On FAT32 root_dir.first_sector as previously calculated must be 0
		//It is actually stored in the fat version dependent part of the BPB and it's the beginning of a cluster chain
		if (drive->root_dir.first_sector)
			goto error;
		drive->root_dir.first_sector = bpb->ver_dep.v32.root_cluster;
	}

	//Check the signature
	if (bpb->signature!=MBR_BOOT_SIG)
		goto error;

	return 0;

error:
	return -1;
}

void fat_print_dir(struct fat_drive drive, uint32_t cluster) {
	struct fat_entry *fat_entry;
	int exit = 0;
	uint64_t where;
	uint32_t entries_per_cluster_current = 0;

	if (cluster==ROOT_DIR_CLUSTER) {    //We want the root dir?
		if (drive.type==FAT16) {
			where = drive.root_dir.first_sector << drive.log_bytes_per_sector;
		} else {
			cluster = drive.root_dir.first_cluster;
			where = first_sector_of_cluster(drive, cluster) << drive.log_bytes_per_sector;
		}
	} else {
		where = first_sector_of_cluster(drive, cluster) << drive.log_bytes_per_sector;
	}

	while (!exit) {
		fat_entry = (struct fat_entry *) drive.read_bytes(where, sizeof(struct fat_entry));
		entries_per_cluster_current++;

		if (fat_entry->attr!=ATTR_LONG_NAME) {
			switch (fat_entry->name.base[0]) {
				case 0xE5u: break;
				case 0x00u: exit = 1;
					continue;
				case 0x05u: //KANJI
					fat_entry->name.base[0] = 0xE5u;
				default: printf("=====================\n");
					printf("File: [%.8s.%.3s]\n", fat_entry->name.base, fat_entry->name.ext);
					print_entry_info(*fat_entry);
					print_lfn(drive, *fat_entry, where);
			}
		}

		//If we are parsing the root directory on FAT16 every entry is contiguous, or
		//are there still entries in the cluster?
		if ((entries_per_cluster_current!=drive.entries_per_cluster) ||
			(drive.type==FAT16 && cluster==ROOT_DIR_CLUSTER)) {
			where += sizeof(struct fat_entry);
		} else {
			//Should we move to the next cluster?
			if (is_eof(drive, cluster)) {    //Nope, if this is the last cluster
				break;
			} else {                            //Move to the next cluster
				cluster = find_next_cluster(drive, cluster);
				where = first_sector_of_cluster(drive, cluster) << drive.log_bytes_per_sector;
				entries_per_cluster_current = 0;
			}
		}
	}
}

void fat_save_file(struct fat_drive drive, uint32_t cluster, uint32_t size_bytes) {
	FILE *f;
	uint8_t *data;
	uint64_t address;
	uint32_t cluster_size_bytes;

	cluster_size_bytes = 1u << (uint32_t) (drive.log_sectors_per_cluster + drive.log_bytes_per_sector);
	f = fopen("../out.pdf", "wb");

	do {
		address = first_sector_of_cluster(drive, cluster) << drive.log_bytes_per_sector;

		if (size_bytes >= cluster_size_bytes) {
			data = drive.read_bytes(address, cluster_size_bytes);
			fwrite(data, cluster_size_bytes, 1, f);
			size_bytes -= cluster_size_bytes;
		} else {
			data = drive.read_bytes(address, size_bytes);
			fwrite(data, size_bytes, 1, f);
			size_bytes = 0;
		}

		cluster = find_next_cluster(drive, cluster);
	} while (!is_eof(drive, cluster) && size_bytes!=0);

	fclose(f);
}

static inline uint32_t first_sector_of_cluster(struct fat_drive drive, uint32_t cluster) {
	return ((cluster - 2) << drive.log_sectors_per_cluster) + drive.first_data_sector;
}

static uint32_t find_next_cluster(struct fat_drive fat, uint32_t current_cluster) {
	uint32_t fat_offset, fat_sector_number, fat_entry_offset;
	uint8_t *data;

	if (fat.type==FAT16)
		fat_offset = current_cluster << 1u;
	else
		fat_offset = current_cluster << 2u;

	fat_sector_number = fat.first_fat_sector + (fat_offset >> fat.log_bytes_per_sector);
	fat_entry_offset = fat_offset & ((1u << fat.log_bytes_per_sector) - 1);

	data = fat.read_bytes(((uint64_t) fat_sector_number << fat.log_bytes_per_sector) + fat_entry_offset, 4);

	if (fat.type==FAT16)
		return *((uint16_t *) data);
	else
		return *((uint32_t *) data) & CLUSTER_MASK_32;
}

static inline int is_eof(struct fat_drive drive, uint32_t cluster) {
	if (drive.type==FAT16)
		return (cluster >= CLUSTER_EOF_16);
	else
		return (cluster >= CLUSTER_EOF_32);
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

static void print_lfn(struct fat_drive drive, struct fat_entry entry, uint64_t where) {
	uint8_t checksum, order;
	struct fat_lfn_entry *lfn_entry;

	order = 1;
	checksum = fat_sfn_checksum(entry.name.base);

	while (1) {
		//We move back of one entry and check if a long entry exists
		where -= sizeof(struct fat_lfn_entry);
		lfn_entry = (struct fat_lfn_entry *) drive.read_bytes(where, sizeof(struct fat_lfn_entry));
		if (((lfn_entry->attr & ATTR_LONG_NAME_MASK)==ATTR_LONG_NAME) && (lfn_entry->checksum==checksum)) {
			if (lfn_entry->order==order) {
				fat_print_lfn_entry(*lfn_entry);
				order++;
			} else if (lfn_entry->order==(order | LAST_LONG_ENTRY)) {
				fat_print_lfn_entry(*lfn_entry);
				order++; //just to check at the end
				break;
			}
		} else {
			break;
		}
	}

	if (order!=1)
		printf("\n");
}
