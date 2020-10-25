#include <stdio.h>
#include "fat.h"
#include "reader.h"
#define BUFFER_SIZE (16384 + 20)

int main() {
	FILE *f;
	uint32_t size;
	fat_file file;
	fat_drive drive;
	uint8_t buffer[BUFFER_SIZE];

	if (fat.mount(&drive, 512, debug_read_bytes))
		goto error;

	printf("Block size: %d Bytes\n", 1u << drive.log_bytes_per_sector);
	printf("LBA begin: %d\n", drive.first_partition_sector);

	{ //Save hamlet.txt, incremental dir change
		fat_dir cd;
		fat.dir_get_root(&cd);
		fat.dir_change(&drive, &cd, "subdir");
		fat.dir_change(&drive, &cd, "..");
		fat.dir_change(&drive, &cd, ".");

		f = fopen("../hamlet.txt", "wb");
		if (fat.file_open_in_dir(&drive, &cd, "hamlet.txt", &file))
			goto error;

		while ((size = fat.file_read(&drive, &file, buffer, BUFFER_SIZE)))
			fwrite(buffer, size, 1, f);

		fclose(f);
	}

	{ //Save 1.txt
		f = fopen("../1.txt", "wb");
		if (fat.file_open(&drive, "/subdir/1.txt", &file))
			goto error;

		while ((size = fat.file_read(&drive, &file, buffer, BUFFER_SIZE)))
			fwrite(buffer, size, 1, f);

		fclose(f);
	}

	{
		fat_dir dir;
		fat.dir_get_root(&dir);
		//fat.dir_change(&drive, &dir, "subdir");

		fat_list_entry entry;
		fat.list_make_empty_entry(&entry);

		while(fat.list_get_next_entry_in_dir(&drive, &dir, &entry))
			printf("%.11s\n", entry.name);
	}


	return 0;

error:
	return -1;
}
