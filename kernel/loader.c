#include "kernel/loader.h"

#define IMG1_HEADER_SIZE 20U
#define IMG1_ENTRY_SIZE 24U
#define IMG1_NAME_SIZE 16U
#define IMG1_MAX_COUNT 32U

_Static_assert( sizeof( struct img1_header ) == IMG1_HEADER_SIZE, "IMG1 header layout" );
_Static_assert( sizeof( struct img1_directory_entry ) == IMG1_ENTRY_SIZE, "IMG1 directory layout" );

/* Decode bytes explicitly: the format is little-endian, and a test or future
 * caller may supply an unaligned buffer. Never cast input bytes to structs. */
static uint32_t read_u32( const uint8_t *p )
{
  return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static unsigned int validate_program_name( const uint8_t *name )
{
  if ( name[0] == 0 )
    return 0;

  for ( unsigned int i = 0; i < IMG1_NAME_SIZE; ++i )
  {
    if ( name[i] == 0 )
      return 1;
    if ( ( name[i] >= 'A' && name[i] <= 'Z' ) ||
         ( name[i] >= 'a' && name[i] <= 'z' ) ||
         ( name[i] >= '0' && name[i] <= '9' ) ||
         name[i] == '-' || name[i] == '_' )
      continue;
    return 0;
  }
  return 0;
}

/* Both names have already passed validation. Ignore bytes after the NUL. */
static unsigned int same_name( const uint8_t *a, const uint8_t *b )
{
  for ( unsigned int i = 0; i < IMG1_NAME_SIZE; ++i )
  {
    if ( a[i] != b[i] )
      return 0;
    if ( a[i] == 0 )
      return 1;
  }
  return 0;
}

unsigned int img1_validate( const void *image, size_t available_size )
{
  if ( image == NULL || available_size < IMG1_HEADER_SIZE )
    return 0;

  const uint8_t *bytes = image;
  if ( bytes[0] != 'I' || bytes[1] != 'M' || bytes[2] != 'G' || bytes[3] != '1' || read_u32( bytes + 4 ) != 1 )
    return 0;

  uint32_t image_size = read_u32( bytes + 8 );
  uint32_t count = read_u32( bytes + 12 );
  uint32_t directory_offset = read_u32( bytes + 16 );
  if ( image_size < IMG1_HEADER_SIZE || image_size > IMG1_MAX_SIZE || image_size > available_size ||
       count == 0 || count > IMG1_MAX_COUNT )
    return 0;

  /* Subtract only after checking the offset, so neither subtraction nor a
   * hostile offset + length can wrap. The count limit makes count * 24 safe. */
  uint32_t directory_size = count * IMG1_ENTRY_SIZE;
  if ( directory_offset < IMG1_HEADER_SIZE || directory_offset % 4 != 0 ||
       directory_offset > image_size || directory_size > image_size - directory_offset )
    return 0;

  /* Both terms are aligned, so the directory end is already rounded to 4. */
  uint32_t contents_start = directory_offset + directory_size;
  for ( uint32_t i = 0; i < count; ++i )
  {
    const uint8_t *entry = bytes + directory_offset + i * IMG1_ENTRY_SIZE;
    uint32_t offset = read_u32( entry + 16 );
    uint32_t size = read_u32( entry + 20 );
    if ( !validate_program_name( entry ) || offset % 4 != 0 || offset < contents_start ||
         size < 40 || offset > image_size || size > image_size - offset )
      return 0;

    /* Earlier files are already bounded, making both end additions safe.
     * Strict inequalities allow adjacent files; directory order is irrelevant. */
    for ( uint32_t j = 0; j < i; ++j )
    {
      const uint8_t *previous = bytes + directory_offset + j * IMG1_ENTRY_SIZE;
      uint32_t previous_offset = read_u32( previous + 16 );
      uint32_t previous_size = read_u32( previous + 20 );
      if ( same_name( entry, previous ) || ( offset < previous_offset + previous_size && previous_offset < offset + size ) )
        return 0;
    }
  }
  return 1;
}
