#include <stdio.h>
#include "fat.h"
#include "reader.h"

int main() {
	struct fat_drive fatDrive;

	if (fat_init(&fatDrive, 512, debug_read_bytes))
		goto error;

	printf("Block size: %d Bytes\n", 1u << fatDrive.log_bytes_per_sector);
	printf("LBA begin: %d\n", fatDrive.first_partition_sector);

	fat_print_dir(fatDrive, 0);
	printf("\n");
	//fat_print_dir(fatDrive, 0xF);
	//fat_save_file(fatDrive, 2, 193082);

	return 0;

error:
	return -1;
}
