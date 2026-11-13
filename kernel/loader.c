#include "kernel/loader.h"
#include <stddef.h>
#include "kernel/task.h"

#define IMG1_HEADER_SIZE 20U
#define IMG1_ENTRY_SIZE 24U
#define IMG1_NAME_SIZE 16U
#define IMG1_MAX_COUNT 32U

extern uint32_t __program_slots_start[];

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

static int find_free_memory_slot()
{
  for ( uint32_t i = 0; i < PROGRAM_SLOT_COUNT; i++ )
    if ( task_program_slot_free( i ) )
      return i;
  return -1;
}

/*To find the entry by program's name*/
static const struct img1_directory_entry *find_directory_by_name( const uint8_t *name )
{
  const struct img1_directory_entry *entries = (const struct img1_directory_entry *) ( (const uint8_t *) IMG1 + IMG1->directory_offset );
  for ( uint32_t i = 0; i < IMG1->count; i++ )
    if ( same_name( name, (const uint8_t *) entries[i].name ) )
      return &entries[i];
  return NULL;
}

static unsigned int validate_elf( const Elf32_Ehdr *ehdr, uint32_t file_size, struct elf_image *elf )
{

  const uint8_t *elf_raw = (const uint8_t *) ehdr;

  if ( file_size < sizeof( Elf32_Ehdr ) )
    return 0;

  /*Header validation*/
  if ( ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
       ehdr->e_ident[EI_MAG1] != ELFMAG1 || /*Magic bytes*/
       ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
       ehdr->e_ident[EI_MAG3] != ELFMAG3 ||
       ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||               /*Elf 32 bit*/
       ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||               /*Little-endian*/
       ehdr->e_ident[EI_VERSION] != EV_CURRENT ||             /*Version 1*/
       ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_ARM ) /*Executable ARM elf*/
    return 0;

  /*Size validation*/
  if ( ehdr->e_ehsize != 52 || ehdr->e_phentsize != 32 || ehdr->e_shentsize != 40 || ehdr->e_shnum == 0 || ehdr->e_phnum == 0 )
    return 0;

  uint32_t ph_table_end = ehdr->e_phoff + ( (uint32_t) ehdr->e_phnum * 32u );
  uint32_t sh_table_end = ehdr->e_shoff + ( (uint32_t) ehdr->e_shnum * 40u );

  if ( ph_table_end > file_size || sh_table_end > file_size )
    return 0;

  const Elf32_Shdr *shdrs = (const Elf32_Shdr *) ( elf_raw + ehdr->e_shoff );
  const uint16_t shdrcount = ehdr->e_shnum;
  const Elf32_Sym *symtable = NULL;
  uint32_t symcount = 0;
  /*Check Symbols size and relocation records, it must not have RELA records, dynamic linking, undefined symbols or relocation types other than the ones accepted for loaded content*/

  for ( uint16_t i = 0; i < shdrcount; i++ )
  {
    if ( shdrs[i].sh_type == SHT_RELA ||   /*RELA section*/
         shdrs[i].sh_type == SHT_DYNAMIC ) /*Dynamic linking section*/
      return 0;

    if ( shdrs[i].sh_type == SHT_SYMTAB )
    { /* Check symbol entries size*/
      if ( shdrs[i].sh_entsize != 16 )
        return 0;
      symtable = (const Elf32_Sym *) ( elf_raw + shdrs[i].sh_offset );
      symcount = symtable->st_size / 16;
    }
    if ( shdrs[i].sh_type != SHT_NOBITS )
    {
      if ( shdrs[i].sh_offset + shdrs[i].sh_size > file_size )
        return 0;
    }
  }

  /*Check relocation sections*/

  for ( uint16_t i = 0; i < shdrcount; i++ )
  {
    if ( shdrs[i].sh_type != SHT_REL )
      continue;

    if ( shdrs[i].sh_entsize != 8 )
      return 0;

    if ( shdrs[i].sh_info >= shdrcount )
      return 0;

    /*Check relocation target*/
    const Elf32_Shdr *target = &shdrs[shdrs[i].sh_info];

    if ( !( target->sh_flags & SHF_ALLOC ) )
      continue;

    if ( !symtable )
      return 0;

    /*Check relocation section*/
    const Elf32_Rel *rels = (const Elf32_Rel *) ( elf_raw + shdrs[i].sh_offset );
    uint32_t relcount = shdrs[i].sh_size / 8;
   for ( uint32_t r = 0; r < relcount; r++ )
    {
      uint32_t r_type = ELF32_R_TYPE( rels[r].r_info );
      uint32_t r_sym = ELF32_R_SYM( rels[r].r_info );

      if ( r_type != R_ARM_ABS32 &&
           r_type != R_ARM_THM_PC22 && /*Old newlib alias for R_ARM_THM_CALL*/
           r_type != R_ARM_THM_JUMP24 )
        return 0;

      if ( rels[r].r_offset + 4 > target->sh_size ) /* The patched target field is 4 bytes wide */
        return 0;

      if ( r_sym >= symcount )
        return 0;

      if ( symtable[r_sym].st_shndx == SHN_UNDEF ) // The symbol must exist in the table
        return 0;
    }
  }

  /*Program headers validation*/
  const Elf32_Phdr *phdrs = (const Elf32_Phdr *) ( elf_raw + ehdr->e_phoff );
  const uint16_t phdrcount = ehdr->e_phnum;
  int loadcount = 0;
  for ( uint16_t i = 0; i < phdrcount; i++ )
  {
    if ( phdrs[i].p_type == PT_LOAD )
    {
      loadcount++;
      /*The segment must be smaller than file_size*/
      if ( phdrs[i].p_offset + phdrs[i].p_filesz > file_size || phdrs[i].p_memsz < phdrs[i].p_filesz )
        return 0;
    }
    /*No dynamic links*/
    if ( phdrs[i].p_type == PT_DYNAMIC || phdrs[i].p_type == PT_INTERP )
      return 0;
  }

  if ( loadcount == 0 )
    return 0;

  if ( ( ehdr->e_entry & 1u ) == 0 ) /*Thumb entry point*/
    return 0;

  /*Fill the structure*/
  elf->ehdr = ehdr;
  elf->entry_point = ehdr->e_entry;
  elf->phdrs = phdrs;
  elf->ph_count = phdrcount;
  elf->shdrs = shdrs;
  elf->sh_count = shdrcount;
  elf->sym = symtable;
  elf->sym_count = symcount;
  return 1;
}

/* Requires a validated ELF whose allocated sections are already loaded. */
static void apply_relocations( const struct elf_image *elf, uintptr_t slot_base )
{
  const uint8_t *elf_bytes = (const uint8_t *) elf->ehdr;

  for ( uint16_t i = 0; i < elf->sh_count; ++i )
  {
    const Elf32_Shdr *section = &elf->shdrs[i];
    if ( section->sh_type != SHT_REL )
      continue;

    const Elf32_Shdr *target = &elf->shdrs[section->sh_info];
    if ( !( target->sh_flags & SHF_ALLOC ) )
      continue;

    const Elf32_Rel *rels = (const Elf32_Rel *) ( elf_bytes + section->sh_offset );
    uint32_t relcount = section->sh_size / section->sh_entsize;
    for ( uint32_t r = 0; r < relcount; ++r )
    {
      if ( ELF32_R_TYPE( rels[r].r_info ) != R_ARM_ABS32 )
        continue;

      uint32_t *site = (uint32_t *) ( slot_base + rels[r].r_offset );
      *site += slot_base;
    }
  }
}

static unsigned int search_elf( const struct img1_directory_entry *directory, struct elf_image *elf )
{
  if ( !directory )
    return 0;
  const uint8_t *base = (const uint8_t *) IMG1 + directory->offset;
  const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *) base;

  return validate_elf( ehdr, directory->size, elf );
}

/*Parses the ELF image of a program, validates its segment boundaries,
* copies PT_LOAD segments from Flash to the assigned RAM slot, zero-initializes
* the .bss sections, and applies ELF relocations.*/
static int copy_data( const struct img1_directory_entry *entry, const int slot )
{
  struct elf_image elf;
  if ( !search_elf( entry, &elf) )
    return 0;

  /*retrieve the RAM boundaries for this slot*/
  uint32_t ram_base = (uint32_t) __program_slots_start + slot * PROGRAM_SLOT_SIZE;
	uint32_t ram_end = ram_base + PROGRAM_SLOT_SIZE;
  /*It finds the starting point in the flash*/
  const uint8_t *flash_base = (const uint8_t *) IMG1 + entry->offset;

  uint8_t *ram_ptr = (uint8_t *) ram_base;

  /*Explore the Program Headers to find the segments to be loaded*/
  for( uint16_t i=0; i<elf.ph_count; i++)
  {
    const Elf32_Phdr *phdr = &elf.phdrs[i];

    if(phdr->p_type == PT_LOAD)
    {
      /* Check that the block to be read does not extend beyond the physical end of the file in the Flash memory*/
      if (phdr->p_offset + phdr->p_filesz > entry->size)
        return 0;
      
      /*Check that the segment does not require more RAM than what has been allocated to the slot*/
      uintptr_t segment_dest_end = ram_base + phdr->p_vaddr + phdr->p_memsz;
      if( segment_dest_end > ram_end)
        return 0;
      
      /*define the source (Flash) and destination (RAM) pointers for the segment*/
      const uint8_t *src = flash_base + phdr->p_offset;
      uint8_t *dest = ram_ptr + phdr->p_vaddr;

      /* copy of the initialized data (.text and .data)*/
      for (uint32_t j = 0; j < phdr->p_filesz; j++)
      {
        dest[j] = src[j];
      }

      /*zeroing of uninitialized memory (.bss)*/
      for (uint32_t j = phdr->p_filesz; j < phdr->p_memsz; j++)
      {
        dest[j] = 0;
      }
    }
  }
apply_relocations(&elf, ram_base);

return 1;
}


/* Load an executable program from the IMG1 Flash image into a free RAM slot,
applies relocations, and creates a corresponding task in the kernel scheduler. */
int load_program( const char *name )
{

  if ( !validate_program_name( (const uint8_t *)name ) || !img1_validate(IMG1,IMG1_MAX_SIZE) )
    return -1;

  const struct img1_directory_entry *entry = find_directory_by_name( (const uint8_t *)name );
  if ( !entry )
    return -2;

  const int free_slot = find_free_memory_slot();
  if ( free_slot < 0 )
    return -3;

  /* Copy segments from Flash to RAM and apply ELF relocations */
  if (!copy_data( entry, free_slot ) )
    return -1;

  /* Re-parse the ELF image to extract runtime metadata (e.g., entry point)*/  
  struct elf_image elf;
  if(!search_elf( entry, &elf))
    return -1;

  /* Determine the task array index and set the stack pointer to the top (end) 
   * of the RAM partition, since the ARM stack grows downward.*/
  uint32_t slot_base = (uint32_t) __program_slots_start + free_slot * PROGRAM_SLOT_SIZE;
  uint32_t *stack_top = (uint32_t *)(slot_base + PROGRAM_SLOT_SIZE);

  /* Register the task in the kernel scheduler*/
  int task_id = task_program_create( free_slot,
                                     name,
                                     (void (*)(void)) (slot_base + elf.entry_point),
                                     stack_top,
                                     1);
  
  return task_id;
} 
