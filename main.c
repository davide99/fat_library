#include <stdio.h>
#include "fat.h"
#include "reader.h"
#define BUFFER_SIZE (16384 + 20)

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

	uint32_t size;

	while ((size = fat_save_file(&drive, &file, buffer, BUFFER_SIZE)))
		fwrite(buffer, size, 1, f);

	fclose(f);

	return 0;

error:
	return -1;
}
