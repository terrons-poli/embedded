#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>

struct img1_header {
	char magic[4];
	uint32_t version;	
	uint32_t image_size;
	uint32_t count;
	uint32_t directory_offset;
};

struct img1_directory_entry {
	char name[16];
	uint32_t offset;
	uint32_t size;
};

#define IMG1 ((struct img1_header*)0x00200000)

#endif // LOADER_H
