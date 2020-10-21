#include <stdint.h>
#include <stdio.h>

//#define IMAGE "/mnt/tmp/king4.img"
#define IMAGE "../image.img"

void *debug_read_bytes(uint64_t address, uint32_t bytes, void *buffer) {
	FILE *f;

	if ((f = fopen(IMAGE, "rb"))==NULL)
		return NULL;

	fseek(f, address, SEEK_SET);
	if (fread(buffer, bytes, 1, f)!=1)
		return NULL;
	fclose(f);

	return buffer;
}
