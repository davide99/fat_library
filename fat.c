#include "fat.h"
#include "fat_types.h"
#include "fat_utils.h"
#include <stdio.h>
#include <string.h>

//Private functions
/*
 * Fat drive is passed around using always a pointer, so that the compiler
 * doesn't need to push the whole fat_drive (+ the buffer) in the stack
 */
static int get_partition_info(struct fat_drive *drive);
static int read_BPB(struct fat_drive *drive);
static uint32_t first_sector_of_cluster(struct fat_drive *drive, uint32_t cluster);
static void print_entry_info(struct fat_entry *entry);
static uint32_t find_next_cluster(struct fat_drive *drive, uint32_t current_cluster);
static int is_eof(struct fat_drive *drive, uint32_t cluster);
static void print_lfn(struct fat_drive *drive, struct fat_entry entry, uint64_t where);

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
	if ((mbr_partition = drive->read_bytes(0x1BEu, sizeof(struct mbr_partition_entry), drive->buffer))==NULL)
		goto error;

	if (mbr_partition->type!=0) {
		drive->first_partition_sector = mbr_partition->lba_begin;
		return 0;
	}

	//Check the signature
	if (*((uint16_t *) drive->read_bytes(510, 2, drive->buffer))!=MBR_BOOT_SIG)
		goto error;

error:
	return -1;
}

static inline int read_BPB(struct fat_drive *drive) {
	struct fat_BPB *bpb;
	uint32_t root_dir_sectors, data_sectors_cluster, fat_size_sectors;

	if ((bpb = drive->read_bytes(
		((uint64_t) drive->first_partition_sector << drive->log_bytes_per_sector)
			+ BPB_BYTE_OFFSET__FROM_PARTITION_BEGIN, sizeof(struct fat_BPB), drive->buffer))==NULL)
		goto error;

	//Should be equal to the previous set size
	if (bpb->bytes_per_sector!=(1u << drive->log_bytes_per_sector))
		goto error;

	//Parse the BPB: save just what we need
	//Size
	drive->log_sectors_per_cluster = fat_log2(bpb->sectors_per_cluster);

	if (bpb->fat_size_sectors_16!=0) {
		fat_size_sectors = bpb->fat_size_sectors_16;
	} else {
		/*
		 * If fat_size_sectors_16==0 we need to read fat_size_sectors_32, which
		 * is stored in the fat version dependent BPB part.
		 * We read it directly into the variable.
		 */
		drive->read_bytes(
			((uint64_t) drive->first_partition_sector << drive->log_bytes_per_sector)
				+ BPB32_BYTE_OFFEST__FAT_SIZE_SECTORS_32, sizeof(fat_size_sectors), &fat_size_sectors
		);
	}

	drive->entries_per_cluster =
		(1u << (uint32_t) (drive->log_sectors_per_cluster + drive->log_bytes_per_sector))/sizeof(struct fat_entry);

	//Pointers
	drive->first_fat_sector = drive->first_partition_sector + bpb->reserved_sectors_count;
	drive->root_dir.first_sector = drive->first_fat_sector + fat_size_sectors*bpb->number_of_fats;

	//Determine fat version, fatgen pag. 14, we need to be extra careful to avoid overflows
	//We end up with (at most) 16+5-9+1=13 bits. However total sectors is 32 bit long
	root_dir_sectors =
		((bpb->root_entries_count << 5u) + ((1u << drive->log_bytes_per_sector) - 1)) >> drive->log_bytes_per_sector;

	drive->first_data_sector = drive->root_dir.first_sector + root_dir_sectors;

	data_sectors_cluster = ((!bpb->total_sectors_16 ? bpb->total_sectors_32 : bpb->total_sectors_16) -
		(drive->first_data_sector - drive->first_partition_sector)) >> drive->log_sectors_per_cluster;

	if (data_sectors_cluster < 4085 || data_sectors_cluster >= 268435445) {
		goto error; //FAT12 or exFAT
	} else if (data_sectors_cluster < 65525) {
		drive->type = FAT16;
	} else {
		drive->type = FAT32;
		//On FAT32 root_dir.first_sector as previously calculated must be 0
		//It is actually stored in the fat version dependent part of the BPB and it's the beginning of a cluster chain
		if (root_dir_sectors)
			goto error;

		drive->read_bytes(
			((uint64_t) drive->first_partition_sector << drive->log_bytes_per_sector)
				+ BPB32_BYTE_OFFEST__ROOT_CLUSTER_32, 4, &drive->root_dir.first_sector
		);
	}

	//Check the signature
	if ((*(uint16_t *) drive->read_bytes(510, 2, drive->buffer))!=MBR_BOOT_SIG)
		goto error;

	return 0;

error:
	return -1;
}

void fat_print_dir(struct fat_drive *drive, uint32_t cluster) {
	struct fat_entry *fat_entry;
	int exit = 0;
	uint64_t where;
	uint32_t parsed_entries_per_cluster = 0;

	if (cluster==ROOT_DIR_CLUSTER) {    //We want the root dir?
		if (drive->type==FAT16) {
			where = drive->root_dir.first_sector << drive->log_bytes_per_sector;
		} else {
			cluster = drive->root_dir.first_cluster;
			where = first_sector_of_cluster(drive, cluster) << drive->log_bytes_per_sector;
		}
	} else {
		where = first_sector_of_cluster(drive, cluster) << drive->log_bytes_per_sector;
	}

	while (!exit) {
		fat_entry = drive->read_bytes(where, sizeof(struct fat_entry), drive->buffer);

		if (fat_entry->attr!=ATTR_LONG_NAME) {
			switch (fat_entry->name.base[0]) {
				case 0xE5u: break;
				case 0x00u: exit = 1;
					continue;
				case 0x05u: //KANJI
					fat_entry->name.base[0] = 0xE5u;
				default: printf("=====================\n");
					printf("File: [%.8s.%.3s]\n", fat_entry->name.base, fat_entry->name.ext);
					print_entry_info(fat_entry);
					print_lfn(drive, *fat_entry, where);
					fflush(stdout);
			}
		}

		//If we are parsing the root directory on FAT16 every entry is contiguous, or
		//are there still entries in the cluster?
		if ((parsed_entries_per_cluster < drive->entries_per_cluster) ||
			(drive->type==FAT16 && cluster==ROOT_DIR_CLUSTER)) {
			where += sizeof(struct fat_entry);
			parsed_entries_per_cluster++;
		} else {
			//Should we move to the next cluster?
			if (is_eof(drive, cluster)) {    //Nope, if this is the last cluster
				break;
			} else {                            //Move to the next cluster
				cluster = find_next_cluster(drive, cluster);
				where = first_sector_of_cluster(drive, cluster) << drive->log_bytes_per_sector;
				parsed_entries_per_cluster = 0;
			}
		}
	}
}

uint32_t fat_save_file(struct fat_drive *drive, struct fat_file *file, void *buffer, uint32_t buffer_len) {
	uint8_t *byte_buffer;
	uint32_t read_size, cluster_size_bytes, ceil_number_of_clusters, total_byte_read;
	uint64_t where;

	total_byte_read = 0;
	byte_buffer = buffer;
	cluster_size_bytes = 1u << (uint32_t) (drive->log_bytes_per_sector + drive->log_sectors_per_cluster);
	ceil_number_of_clusters =
		1 + ((buffer_len - 1) >> (uint32_t) (drive->log_bytes_per_sector + drive->log_sectors_per_cluster));

	for (uint32_t cluster_offset = 0; cluster_offset < ceil_number_of_clusters; cluster_offset++) {//For each cluster
		read_size = cluster_size_bytes - file->in_cluster_byte_offset;
		if (read_size > file->size_bytes)
			read_size = file->size_bytes;
		where = (first_sector_of_cluster(drive, file->cluster) << drive->log_bytes_per_sector)
			+ file->in_cluster_byte_offset;

		if (read_size <= buffer_len) { //Do we have enough room?
			drive->read_bytes(where, read_size, byte_buffer);
			byte_buffer += read_size;
			file->in_cluster_byte_offset += read_size;
			file->size_bytes -= read_size;
			total_byte_read += read_size;
			buffer_len -= read_size;
		} else { //no
			drive->read_bytes(where, buffer_len, byte_buffer);
			byte_buffer += buffer_len;
			file->in_cluster_byte_offset += buffer_len;
			file->size_bytes -= buffer_len;
			total_byte_read += buffer_len;
			buffer_len = 0;
		}

		if (file->in_cluster_byte_offset==cluster_size_bytes) {
			file->cluster = find_next_cluster(drive, file->cluster);
			file->in_cluster_byte_offset = 0;
		}
	}

	return total_byte_read;
}

static inline uint32_t first_sector_of_cluster(struct fat_drive *drive, uint32_t cluster) {
	return ((cluster - 2) << drive->log_sectors_per_cluster) + drive->first_data_sector;
}

static uint32_t find_next_cluster(struct fat_drive *drive, uint32_t current_cluster) {
	uint32_t fat_offset, fat_sector_number, fat_entry_offset;

	if (drive->type==FAT16)
		fat_offset = current_cluster << 1u;
	else
		fat_offset = current_cluster << 2u;

	fat_sector_number = drive->first_fat_sector + (fat_offset >> drive->log_bytes_per_sector);
	fat_entry_offset = fat_offset & ((1u << drive->log_bytes_per_sector) - 1);

	if (drive->type==FAT16) {
		drive->read_bytes(
			((uint64_t) fat_sector_number << drive->log_bytes_per_sector) + fat_entry_offset, 2, drive->buffer);

		return *((uint16_t *) drive->buffer);
	} else {
		drive->read_bytes(
			((uint64_t) fat_sector_number << drive->log_bytes_per_sector) + fat_entry_offset, 4, drive->buffer);

		return (*((uint32_t *) drive->buffer)) & CLUSTER_MASK_32;
	}
}

static inline int is_eof(struct fat_drive *drive, uint32_t cluster) {
	if (drive->type==FAT16)
		return (cluster >= CLUSTER_EOF_16);
	else
		return (cluster >= CLUSTER_EOF_32);
}

static void print_entry_info(struct fat_entry *entry) {
	printf("  Modified: ");
	fat_print_date(entry->write.date);
	printf(" ");
	fat_print_time(entry->write.time);
	printf("    Start: %d    Size: %d\n",
		   fat_make_dword(entry->first_cluster_high, entry->first_cluster_low), entry->file_size_bytes);

	printf("  Created: ");
	fat_print_date(entry->creation.date);
	printf(" ");
	fat_print_time_tenth(entry->creation.time, entry->creation.time_tenth_of_secs);
	printf("\n");

	printf("  Last access: ");
	fat_print_date(entry->last_access_date);
	printf("\n");

	printf("  ");
	fat_print_entry_attr(entry->attr);
	printf("\n");
}

static void print_lfn(struct fat_drive *drive, struct fat_entry entry, uint64_t where) {
	uint8_t checksum, order;
	struct fat_lfn_entry *lfn_entry;

	checksum = fat_sfn_checksum(entry.name.base);

	for (order = 1; order <= MAX_ORDER_LFS_ENTRIES; order++) {
		//We move back of one entry and check if a long entry exists
		where -= sizeof(struct fat_lfn_entry);
		lfn_entry = drive->read_bytes(where, sizeof(struct fat_lfn_entry), drive->buffer);

		if (((lfn_entry->attr & ATTR_LONG_NAME_MASK)==ATTR_LONG_NAME) && (lfn_entry->checksum==checksum)
			&& (lfn_entry->type==0)) { //0=name entry
			if (lfn_entry->order==order) {
				fat_print_lfn_entry(*lfn_entry);
			} else if (lfn_entry->order==(order | LAST_LONG_ENTRY)) {
				fat_print_lfn_entry(*lfn_entry);
				order = MAX_ORDER_LFS_ENTRIES + 1; //force the for to exit
			}
		} else {
			order = MAX_ORDER_LFS_ENTRIES + 1; //force the for to exit
		}
	}

	if (order!=1)
		printf("\n");
}
