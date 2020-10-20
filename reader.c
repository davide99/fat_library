#include <stdint.h>
#include <stdio.h>

//TODO: fix size
#define BUFFER_SIZE 16384
uint8_t buffer[BUFFER_SIZE];

//#define IMAGE "/mnt/tmp/king4.img"
#define IMAGE "../image.img"

uint8_t *debug_read_bytes(uint64_t address, uint32_t bytes) {
	FILE *f;

	if ((f = fopen(IMAGE, "rb"))==NULL)
		return NULL;

	fseek(f, address, SEEK_SET);
	if (fread(buffer, bytes, 1, f)!=1)
		return NULL;
	fclose(f);

	return buffer;
}
