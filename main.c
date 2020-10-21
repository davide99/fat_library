#include <stdio.h>
#include "fat.h"
#include "reader.h"

int main() {
	struct fat_drive drive;

	if (fat_init(&drive, 512, debug_read_bytes))
		goto error;

	printf("Block size: %d Bytes\n", 1u << drive.log_bytes_per_sector);
	printf("LBA begin: %d\n", drive.first_partition_sector);

	printf("\n+++++++++++++++++++\n");
	fat_print_dir(&drive, 0);
	printf("\n+++++++++++++++++++\n");
	fat_print_dir(&drive, 15);
	//fat_save_file(fatDrive, 2, 193082);

	return 0;

error:
	return -1;
}
