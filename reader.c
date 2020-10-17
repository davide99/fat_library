#include <stdint.h>
#include <stdio.h>

//TODO: fix size
#define BUFFER_SIZE 16384
uint8_t buffer[BUFFER_SIZE];

uint8_t *debug_read_bytes(uint64_t address, uint32_t bytes) {
    FILE *f;

    if ((f = fopen("../image.img", "rb")) == NULL)
        return NULL;

    fseek(f, address, SEEK_SET);
    if (fread(buffer, bytes, 1, f) != 1)
        return NULL;
    fclose(f);

    return buffer;
}
