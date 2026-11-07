#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>

#define IMG1_MAX_SIZE ( 1UL << 21 )
#define IMG1 ( (const struct img1_header *) 0x00200000 )
#define IMG1_NAME_SIZE 16U

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

/* Number of directory entries in the FLASH image at IMG1.
 * Returns 1..32, or -1 when the image breaks assignment section 6.1.
 * The image is read in place: nothing is copied and nothing is printed. */
int img1_count( void );
/* Copy directory entry `index` into `name`, for SVC 12.
 * Returns 0 after copying the whole name and its NUL. Returns -1 without
 * writing when `capacity` is not 1..IMG1_NAME_SIZE, `name` is NULL, the
 * image is invalid, or `index` is not an entry. When the name does not fit,
 * copies the leading characters that do, terminates them with NUL, and
 * returns -1. The caller must already have checked that the whole destination
 * lies in the caller's RAM partition. FLASH is not a valid destination. */
int img1_name( unsigned index, char *name, unsigned capacity );

#endif // LOADER_H
