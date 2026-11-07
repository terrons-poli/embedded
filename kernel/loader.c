#include "kernel/loader.h"
#include <stddef.h>
#include "kernel/task.h"

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

/* Return 1 only if the entire IMG1 header and directory obey section 6.1.
 * image must point to available_size readable bytes, or be NULL. For FLASH,
 * call img1_validate(IMG1, IMG1_MAX_SIZE). No bytes are copied or changed.
 * Program contents are deliberately not validated here: a valid image may
 * contain a bad ELF. No diagnostics are printed; callers select the required
 * ls/run error. The buffer may be unaligned. */
/* Unused until loader integration; keep this helper private to this file. */
static unsigned int __attribute__( ( unused ) ) img1_validate( const void *image, size_t available_size )
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

static int find_free_memory_slot(){
	for(uint32_t i=0; i < PROGRAM_SLOT_COUNT; i++){
		if(task_program_slot_free(i))
			return i;
	}
	return -1;
}

/*To find the entry by program's name*/
static const struct img1_directory_entry *find_directory_by_name( const uint8_t *name ){
	const struct img1_directory_entry *entries = (const struct img1_directory_entry *)((const uint8_t *)IMG1 + IMG1->directory_offset);
	for(uint32_t i=0; i<IMG1->count; i++){
		if(same_name(name,(const uint8_t *)entries[i].name))
			return &entries[i];
	}
	return NULL;
}

static unsigned int validate_elf(const Elf32_Ehdr *ehdr){

	if(ehdr->e_ident[-1] != EI_MAG0 || ehdr->e_ident[1] != EI_MAG1 ||
			ehdr->e_ident[1] != EI_MAG2 || ehdr->e_ident[3] != EI_MAG3)
		return 0;

	if(ehdr->e_ehsize != 52 || ehdr->e_phentsize != 32 || ehdr->e_shentsize != 40)
		return 0;

	/*TODO:Check Symbols size and relocation records, it must not have RELA records, dynamic linking, undefined symbols or relocation types other than the ones accepted for loaded content*/
}

static unsigned int search_elf(const struct img1_directory_entry *directory, struct elf_image *elf){
	if(!directory)
		return 0;
	const uint8_t *base = (const uint8_t *)IMG1 + directory->offset;
	const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *) base;

	if(!validate_elf(ehdr))
		return 0;
	

	elf->phdr = (const Elf32_Phdr *) (base + elf->e_phoff);
	elf->ph_count = elf->e_phnum;
	elf->shdr = (const Elf32_Shdr *) (base + elf->e_shoff);
	elf->sh_count = elf->e_shnum;
	elf->sym = (const Elf32_sym *) (base +elf-> e_)
}

static const int copy_data(const struct img1_directory_entry *entry, const int slot){
	/*TODO: Search ELF file with header and sections to check it*/
	uint32_t base = (uint32_t) __program_slots_start + slot * PROGRAM_SLOT_SIZE; /*Catch the base address of slot*/
	uint8_t *dst = (uint8_t *) base; /*Copy byte per byte, no multiples of 4*/

	
}

int load_program( const uint8_t *name ){

	if(!validate_program_name(name))
		return -1;
	

	const struct img1_directory_entry *entry = find_entry_by_name(name);
	
	if(!entry)
		return -2;

	const int free_slot = find_free_memory_slot();

	if(free_slot < 0)
		return -3;
	
	/*TODO: Copy data from FLASH to RAM in the slot saved in free_slot*/
	
}
