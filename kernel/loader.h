#ifndef LOADER_H
#define LOADER_H

#include <stddef.h>
#include <stdint.h>

#define IMG1_MAX_SIZE ( 1UL << 21 )
#define IMG1 ( (const struct img1_header *) 0x00200000 )

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

/* Return 1 only if the entire IMG1 header and directory obey section 6.1.
 * image must point to available_size readable bytes, or be NULL. For FLASH,
 * call img1_validate(IMG1, IMG1_MAX_SIZE). No bytes are copied or changed.
 * Program contents are deliberately not validated here: a valid image may
 * contain a bad ELF. No diagnostics are printed; callers select the required
 * ls/run error. The buffer may be unaligned. */
unsigned int img1_validate( const void *image, size_t available_size );

#endif // LOADER_H
