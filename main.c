#include <stdio.h>
#define FAT_DEBUG
#include "fat.h"
#include "reader.h"

int main() {
	struct fat_drive fatDrive;

	if (fat_init(&fatDrive, 512, debug_read_bytes))
		goto error;

	printf("Block size: %d Bytes\n", 1u << fatDrive.log_sector_size);
	printf("LBA begin: %d\n", fatDrive.lba_begin);

	fat_print_dir(&fatDrive, 0);
	printf("\n");
	fat_print_dir(&fatDrive, 0xF);

	return 0;

error:
	return -1;
}
