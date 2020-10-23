#include <stdio.h>
#include "fat.h"
#include "reader.h"
#define BUFFER_SIZE 512

int main() {
	struct fat_drive drive;
	uint8_t buffer[BUFFER_SIZE];

	if (fat_init(&drive, 512, debug_read_bytes))
		goto error;

	printf("Block size: %d Bytes\n", 1u << drive.log_bytes_per_sector);
	printf("LBA begin: %d\n", drive.first_partition_sector);

	printf("\n+++++++++++++++++++\n");
	fat_print_dir(&drive, 0);
	printf("\n+++++++++++++++++++\n");
	fat_print_dir(&drive, 15);

	FILE *f = fopen("../out.txt", "wb");

	struct fat_file file = {
		.cluster = 2,
		.in_cluster_byte_offset=0,
		.size_bytes=193082
	};

	uintptr_t size;
	int i=0;

	while ((size = fat_save_file(&drive, &file, buffer, BUFFER_SIZE))) {
		fwrite(buffer, size, 1, f);
		i++;
	}

	fclose(f);

	return 0;

error:
	return -1;
}
