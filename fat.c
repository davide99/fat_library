#include "fat.h"
#include "fat_types.h"
#include "fat_utils.h"
#include <string.h>

#define FAT_LIST_ENTRY_CLUSTER_GUARD_VALUE (0xFFFFFFFFu)

//Private functions
static int get_partition_info(fat_drive *drive);
static int read_BPB(fat_drive *drive);
static uint32_t first_sector_of_cluster(fat_drive *drive, uint32_t cluster);
static uint32_t find_next_cluster(fat_drive *drive, uint32_t current_cluster);
static int is_eof(fat_drive *drive, uint32_t cluster);
static int get_entry(fat_drive *drive, fat_dir dir, void *entry, int is_entry_dir, const char *entry_name);

int fat_mount(fat_drive *drive, uint32_t sector_size, fat_read_bytes_func_t read_bytes_func) {
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

static inline int get_partition_info(fat_drive *drive) {
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

static inline int read_BPB(fat_drive *drive) {
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

	drive->cluster_size_bytes = 1u << (uint32_t) (drive->log_bytes_per_sector + drive->log_sectors_per_cluster);

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

uint32_t fat_file_read(fat_drive *drive, fat_file *file, void *buffer, uint32_t buffer_len) {
	uint8_t *byte_buffer = buffer;
	uint32_t read_size, ceil_clusters_to_read, total_byte_read, read_clusters;
	uint64_t where;

	total_byte_read = 0; //Byte that we actually read

	//https://stackoverflow.com/a/2745086/6441490
	ceil_clusters_to_read =
		1 + ((buffer_len - 1) >> (uint32_t) (drive->log_bytes_per_sector + drive->log_sectors_per_cluster));

	//Do the data we read span more clusters?
	for (read_clusters = 0; read_clusters < ceil_clusters_to_read && !is_eof(drive, file->cluster); read_clusters++) {
		if (file->in_cluster_byte_offset==drive->cluster_size_bytes) { //Go to the next cluster?
			file->cluster = find_next_cluster(drive, file->cluster);
			file->in_cluster_byte_offset = 0;
		}

		read_size = drive->cluster_size_bytes - file->in_cluster_byte_offset; //Bytes till the end of the cluster
		if (read_size > file->size_bytes)
			read_size = file->size_bytes;

		where = (first_sector_of_cluster(drive, file->cluster) << drive->log_bytes_per_sector)
			+ file->in_cluster_byte_offset;

		if (read_size > buffer_len) //Do we have enough room in the buffer?
			read_size = buffer_len;

		drive->read_bytes(where, read_size, byte_buffer);
		byte_buffer += read_size; //Move the buffer pointer forward
		file->in_cluster_byte_offset += read_size;
		file->size_bytes -= read_size; //Remaining file size
		total_byte_read += read_size;
		buffer_len -= read_size;
	}

	return total_byte_read;
}

static inline uint32_t first_sector_of_cluster(fat_drive *drive, uint32_t cluster) {
	return ((cluster - 2) << drive->log_sectors_per_cluster) + drive->first_data_sector;
}

static uint32_t find_next_cluster(fat_drive *drive, uint32_t current_cluster) {
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

static inline int is_eof(fat_drive *drive, uint32_t cluster) {
	if (drive->type==FAT16)
		return (cluster >= CLUSTER_EOF_16);
	else
		return (cluster >= CLUSTER_EOF_32);
}

void fat_dir_get_root(fat_dir *dir) {
	dir->cluster = FAT_ROOT_DIR_CLUSTER;
}

int get_entry(fat_drive *drive, fat_dir dir, void *entry, int is_entry_dir, const char *entry_name) {
	struct fat_entry *fat_entry;
	uint64_t where;
	uint32_t parsed_entries_per_cluster = 0;

	if (dir.cluster==FAT_ROOT_DIR_CLUSTER) {    //Are we in the root dir?
		if (drive->type==FAT16) {
			where = drive->root_dir.first_sector << drive->log_bytes_per_sector;
		} else {
			dir.cluster = drive->root_dir.first_cluster;
			where = first_sector_of_cluster(drive, dir.cluster) << drive->log_bytes_per_sector;
		}
	} else {
		where = first_sector_of_cluster(drive, dir.cluster) << drive->log_bytes_per_sector;
	}

	while (1) {
		fat_entry = drive->read_bytes(where, sizeof(struct fat_entry), drive->buffer);

		if (fat_entry->attr!=ATTR_LONG_NAME && fat_entry->name.whole[0]!=0xE5) { //No LFN & deleted entries
			if (fat_entry->name.whole[0]==0x00u) {
				goto not_found;
			} else {
				if (fat_entry->name.whole[0]==0x05u)
					fat_entry->name.whole[0] = 0xE5u;

				if (fat_entry_ascii_name_equals(*fat_entry, entry_name)) { //Found
					if (is_entry_dir) {
						((fat_dir *) entry)->cluster =
							fat_make_dword(fat_entry->first_cluster_high, fat_entry->first_cluster_low);
					} else {
						((fat_file *) entry)->cluster =
							fat_make_dword(fat_entry->first_cluster_high, fat_entry->first_cluster_low);
						((fat_file *) entry)->size_bytes = fat_entry->file_size_bytes;
					}
					return 0;
				}
			}
		}

		//If we are parsing the root directory on FAT16 every entry is contiguous, or
		//are there still entries in the cluster?
		if ((parsed_entries_per_cluster < drive->entries_per_cluster) ||
			(drive->type==FAT16 && dir.cluster==FAT_ROOT_DIR_CLUSTER)) {
			where += sizeof(struct fat_entry);
			parsed_entries_per_cluster++;
		} else {
			//Should we move to the next cluster?
			if (is_eof(drive, dir.cluster)) { //Nope, if this is the last cluster
				goto not_found;
			} else { //Move to the next cluster
				dir.cluster = find_next_cluster(drive, dir.cluster);
				where = first_sector_of_cluster(drive, dir.cluster) << drive->log_bytes_per_sector;
				parsed_entries_per_cluster = 0;
			}
		}
	}

not_found:
	return -1;
}

int fat_dir_change(fat_drive *drive, fat_dir *dir, const char *dir_name) {
	if (dir_name[0]=='.' && dir_name[1]=='\0') //same dir?
		return 1;

	return get_entry(drive, *dir, dir, 1, dir_name);
}

int fat_file_open_in_dir(fat_drive *drive, fat_dir *dir, const char *filename, fat_file *file) {
	file->in_cluster_byte_offset = 0;
	return get_entry(drive, *dir, file, 0, filename);
}

int fat_file_open(fat_drive *drive, const char *path, fat_file *file) {
	char buffer[13];
	int is_last;
	fat_dir dir;

	fat_dir_get_root(&dir);

	while (1) {
		path += fat_split_path(path, buffer, &is_last);
		if (is_last)
			break;
		fat_dir_change(drive, &dir, buffer);
	}

	return fat_file_open_in_dir(drive, &dir, buffer, file);
}

void fat_list_make_empty_entry(fat_list_entry *list_entry) {
	list_entry->next_entry.cluster = FAT_LIST_ENTRY_CLUSTER_GUARD_VALUE;
}

int fat_list_get_next_entry_in_dir(fat_drive *drive, fat_dir *current_dir, fat_list_entry *list_entry) {
	struct fat_entry e;

	if (list_entry->next_entry.cluster==FAT_LIST_ENTRY_CLUSTER_GUARD_VALUE) {
		list_entry->next_entry.cluster = current_dir->cluster;
		list_entry->next_entry.in_cluster_byte_offset = 0;
		list_entry->next_entry.size_bytes = sizeof(struct fat_entry);
	}

	if (list_entry->next_entry.cluster==FAT_ROOT_DIR_CLUSTER && drive->type==FAT16) {
		drive->read_bytes((drive->root_dir.first_sector << drive->log_bytes_per_sector)
							  + list_entry->next_entry.in_cluster_byte_offset, sizeof(struct fat_entry), &e);
		list_entry->next_entry.in_cluster_byte_offset += 32;
	} else {
		fat_file_read(drive, &list_entry->next_entry, &e, FAT_INTERNAL_BUFFER_SIZE);
	}

	if (e.name.whole[0]==0) {
		return 0;
	} else {
		memcpy(list_entry->name, e.name.whole, 11);
		list_entry->next_entry.size_bytes = sizeof(struct fat_entry);
		return 1;
	}
}

const struct m_fat fat = {
	.mount = fat_mount,
	.file_open = fat_file_open,
	.file_open_in_dir = fat_file_open_in_dir,
	.file_read = fat_file_read,

	.dir_get_root = fat_dir_get_root,
	.dir_change = fat_dir_change,

	.list_make_empty_entry = fat_list_make_empty_entry,
	.list_get_next_entry_in_dir = fat_list_get_next_entry_in_dir
};
