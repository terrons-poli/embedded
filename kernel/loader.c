#include "kernel/loader.h"
#include <stddef.h>
#include <stdint.h>
#include "kernel/task.h"
#include "kernel/kprintf.h"
#include "elf.h"

#define IMG1_HEADER_SIZE 20U
#define IMG1_ENTRY_SIZE 24U
#define IMG1_NAME_SIZE 16U
#define IMG1_MAX_COUNT 32U
#define MAX_PROGRAM_SIZE ( PROGRAM_SLOT_SIZE - PROGRAM_STACK_SIZE )

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

/* Names are bounded and NUL-terminated. Ignore bytes after the NUL. */
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

static unsigned int img1_validate( const void *image, size_t available_size )
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

/* Check by subtraction so hostile offset + size values cannot wrap. */
static unsigned int range_fits( uint32_t offset, uint32_t size, uint32_t limit )
{
  return offset <= limit && size <= limit - offset;
}

/* Sections describe relocation targets; segments describe the bytes copied.
 * Require both descriptions to agree, including file offsets for stored data. */
static unsigned int section_is_loaded( const Elf32_Shdr *section,
                                       const Elf32_Phdr *phdrs, uint16_t count )
{
  for ( uint16_t i = 0; i < count; ++i )
  {
    const Elf32_Phdr *segment = &phdrs[i];
    if ( segment->p_type != PT_LOAD || section->sh_addr < segment->p_vaddr )
      continue;
    uint32_t offset = section->sh_addr - segment->p_vaddr;
    if ( !range_fits( offset, section->sh_size, segment->p_memsz ) ||
         ( ( section->sh_flags & SHF_WRITE ) && !( segment->p_flags & PF_W ) ) ||
         ( ( section->sh_flags & SHF_EXECINSTR ) && !( segment->p_flags & PF_X ) ) )
      continue;
    if ( section->sh_type != SHT_NOBITS &&
         ( !range_fits( offset, section->sh_size, segment->p_filesz ) ||
           section->sh_offset != segment->p_offset + offset ) )
      continue;
    return 1;
  }
  return 0;
}

static unsigned int validate_elf( const Elf32_Ehdr *ehdr, uint32_t file_size, struct elf_image *elf )
{
  const uint8_t *elf_raw = (const uint8_t *) ehdr;
  if ( file_size < sizeof( Elf32_Ehdr ) )
    return 0;
  if ( ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
       ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3 ||
       ehdr->e_ident[EI_CLASS] != ELFCLASS32 || ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
       ehdr->e_ident[EI_VERSION] != EV_CURRENT || ehdr->e_version != EV_CURRENT ||
       ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_ARM )
    return 0;

  /* The IMG1 payload is word-aligned. Reject unaligned tables before casting
   * their bytes to ELF records, which require word alignment on this target. */
  if ( ehdr->e_ehsize != sizeof( Elf32_Ehdr ) ||
       ehdr->e_phentsize != sizeof( Elf32_Phdr ) || ehdr->e_shentsize != sizeof( Elf32_Shdr ) ||
       ehdr->e_phnum == 0 || ehdr->e_shnum == 0 ||
       ehdr->e_phoff % 4 != 0 || ehdr->e_shoff % 4 != 0 ||
       !range_fits( ehdr->e_phoff, (uint32_t) ehdr->e_phnum * sizeof( Elf32_Phdr ), file_size ) ||
       !range_fits( ehdr->e_shoff, (uint32_t) ehdr->e_shnum * sizeof( Elf32_Shdr ), file_size ) )
    return 0;

  const Elf32_Phdr *phdrs = (const Elf32_Phdr *) ( elf_raw + ehdr->e_phoff );
  const Elf32_Shdr *shdrs = (const Elf32_Shdr *) ( elf_raw + ehdr->e_shoff );
  uint32_t entry = ehdr->e_entry & ~1u;
  unsigned int entry_loaded = 0;
  unsigned int starts_at_zero = 0;
  unsigned int code_found = 0;
  uint32_t code_size = 0;

  for ( uint16_t i = 0; i < ehdr->e_phnum; ++i )
  {
    const Elf32_Phdr *segment = &phdrs[i];
    if ( segment->p_type == PT_DYNAMIC || segment->p_type == PT_INTERP )
      return 0;
    if ( segment->p_type != PT_LOAD )
      continue;
    if ( !range_fits( segment->p_offset, segment->p_filesz, file_size ) ||
         segment->p_filesz > segment->p_memsz ||
         !range_fits( segment->p_vaddr, segment->p_memsz, MAX_PROGRAM_SIZE ) )
      return 0;
    if ( segment->p_vaddr == 0 && segment->p_memsz != 0 )
      starts_at_zero = 1;
    if ( ( segment->p_flags & PF_X ) && entry >= segment->p_vaddr &&
         range_fits( entry - segment->p_vaddr, 2, segment->p_filesz ) )
      entry_loaded = 1;

    if ( segment->p_flags & PF_X )
    {
      if ( segment->p_vaddr != 0 ||
           segment->p_flags & PF_W ||
           code_found != 0 )
        return 0;
      code_size = segment->p_memsz;
      code_found = 1;
    }

    /* Overlapping copies could overwrite already checked executable bytes. */
    for ( uint16_t j = 0; j < i; ++j )
      if ( phdrs[j].p_type == PT_LOAD && segment->p_memsz && phdrs[j].p_memsz &&
           segment->p_vaddr < phdrs[j].p_vaddr + phdrs[j].p_memsz &&
           phdrs[j].p_vaddr < segment->p_vaddr + segment->p_memsz )
        return 0;
  }

  if ( !starts_at_zero || !entry_loaded || !( ehdr->e_entry & 1u ) )
    return 0;

  uint32_t code_end = ( code_size + 511 ) / 512 * 512;
  for ( uint16_t i = 0; i < ehdr->e_phnum; ++i )
  {
    if ( phdrs[i].p_type == PT_LOAD && !( phdrs[i].p_flags & PF_X ) && phdrs[i].p_memsz != 0 )
    {
      if ( phdrs[i].p_vaddr < code_end )
        return 0;
    }
  }

  for ( uint16_t i = 0; i < ehdr->e_shnum; ++i )
  {
    const Elf32_Shdr *section = &shdrs[i];
    if ( section->sh_type == SHT_RELA || section->sh_type == SHT_DYNAMIC )
      return 0;
    if ( section->sh_type != SHT_NOBITS &&
         !range_fits( section->sh_offset, section->sh_size, file_size ) )
      return 0;
    if ( section->sh_type == SHT_SYMTAB &&
         ( section->sh_entsize != sizeof( Elf32_Sym ) || section->sh_offset % 4 != 0 ||
           section->sh_size % sizeof( Elf32_Sym ) != 0 ) )
      return 0;
    if ( section->sh_type == SHT_REL &&
         ( section->sh_entsize != sizeof( Elf32_Rel ) || section->sh_offset % 4 != 0 ||
           section->sh_size % sizeof( Elf32_Rel ) != 0 || section->sh_info >= ehdr->e_shnum ) )
      return 0;
    if ( !( section->sh_flags & SHF_ALLOC ) )
      continue;
    if ( !range_fits( section->sh_addr, section->sh_size, MAX_PROGRAM_SIZE ) ||
         !section_is_loaded( section, phdrs, ehdr->e_phnum ) )
      return 0;
    for ( uint16_t j = 0; j < i; ++j )
      if ( ( shdrs[j].sh_flags & SHF_ALLOC ) && section->sh_size && shdrs[j].sh_size &&
           section->sh_addr < shdrs[j].sh_addr + shdrs[j].sh_size &&
           shdrs[j].sh_addr < section->sh_addr + section->sh_size )
        return 0;
  }

  for ( uint16_t i = 0; i < ehdr->e_shnum; ++i )
  {
    const Elf32_Shdr *section = &shdrs[i];
    if ( section->sh_type != SHT_REL )
      continue;
    const Elf32_Shdr *target = &shdrs[section->sh_info];
    if ( !( target->sh_flags & SHF_ALLOC ) )
      continue;
    if ( target->sh_type == SHT_NOBITS || section->sh_link >= ehdr->e_shnum ||
         shdrs[section->sh_link].sh_type != SHT_SYMTAB )
      return 0;
    /* Each REL section selects its own symbol table through sh_link. */
    const Elf32_Shdr *symbols = &shdrs[section->sh_link];
    const Elf32_Sym *symtable = (const Elf32_Sym *) ( elf_raw + symbols->sh_offset );
    uint32_t symcount = symbols->sh_size / sizeof( Elf32_Sym );
    const Elf32_Rel *rels = (const Elf32_Rel *) ( elf_raw + section->sh_offset );
    uint32_t relcount = section->sh_size / sizeof( Elf32_Rel );
    for ( uint32_t r = 0; r < relcount; ++r )
    {
      uint32_t type = ELF32_R_TYPE( rels[r].r_info );
      uint32_t symbol = ELF32_R_SYM( rels[r].r_info );
      if ( type != R_ARM_ABS32 && type != R_ARM_THM_PC22 && type != R_ARM_THM_JUMP24 )
      {
        kprintf( "loader: unsupported relocation type %u\n", type );
        return 0;
      }
      if ( rels[r].r_offset < target->sh_addr ||
           !range_fits( rels[r].r_offset - target->sh_addr, 4, target->sh_size ) ||
           rels[r].r_offset % ( type == R_ARM_ABS32 ? 4u : 2u ) != 0 ||
           symbol >= symcount || symtable[symbol].st_shndx == SHN_UNDEF ||
           ( symtable[symbol].st_shndx >= ehdr->e_shnum && symtable[symbol].st_shndx != SHN_ABS ) )
        return 0;
      /* An ABS32 word must be rebased once, even across REL sections. */
      if ( type == R_ARM_ABS32 )
        for ( uint16_t j = 0; j <= i; ++j )
        {
          if ( shdrs[j].sh_type != SHT_REL || !( shdrs[shdrs[j].sh_info].sh_flags & SHF_ALLOC ) )
            continue;
          const Elf32_Rel *previous = (const Elf32_Rel *) ( elf_raw + shdrs[j].sh_offset );
          uint32_t count = j == i ? r : shdrs[j].sh_size / sizeof( Elf32_Rel );
          for ( uint32_t k = 0; k < count; ++k )
            if ( previous[k].r_offset == rels[r].r_offset && ELF32_R_TYPE( previous[k].r_info ) == R_ARM_ABS32 )
              return 0;
        }
    }
  }

  elf->ehdr = ehdr;
  elf->entry_point = ehdr->e_entry;
  elf->phdrs = phdrs;
  elf->ph_count = ehdr->e_phnum;
  elf->shdrs = shdrs;
  elf->sh_count = ehdr->e_shnum;
  elf->code_size = code_size;
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
static int copy_data( const struct img1_directory_entry *entry, uint32_t ram_base, struct elf_image *elf )
{

  if ( !search_elf( entry, elf ) )
    return 0;

  /*It finds the starting point in the flash*/
  const uint8_t *flash_base = (const uint8_t *) IMG1 + entry->offset;

  uint8_t *ram_ptr = (uint8_t *) ram_base;

  /*Explore the Program Headers to find the segments to be loaded*/
  for ( uint16_t i = 0; i < elf->ph_count; i++ )
  {
    const Elf32_Phdr *phdr = &elf->phdrs[i];

    if ( phdr->p_type == PT_LOAD )
    {
      /*define the source (Flash) and destination (RAM) pointers for the segment*/
      const uint8_t *src = flash_base + phdr->p_offset;
      uint8_t *dest = ram_ptr + phdr->p_vaddr;

      /* copy of the initialized data (.text and .data)*/
      for ( uint32_t j = 0; j < phdr->p_filesz; j++ )
        dest[j] = src[j];

      /*zeroing of uninitialized memory (.bss)*/
      for ( uint32_t j = phdr->p_filesz; j < phdr->p_memsz; j++ )
        dest[j] = 0;
    }
  }
  apply_relocations( elf, ram_base );

  return 1;
}

/* Load an executable program from the IMG1 Flash image into a free RAM slot,
applies relocations, and creates a corresponding task in the kernel scheduler. */
int load_program( const char *name )
{

  if ( !img1_validate( IMG1, IMG1_MAX_SIZE ) )
    return -1;

  const struct img1_directory_entry *entry = find_directory_by_name( (const uint8_t *) name );
  if ( !entry )
    return -2;

  const int free_slot = find_free_memory_slot();
  if ( free_slot < 0 )
    return -3;

  uint32_t slot_base = (uint32_t) __program_slots_start + free_slot * PROGRAM_SLOT_SIZE;
  struct elf_image elf;
  /* Copy segments from Flash to RAM and apply ELF relocations */
  if ( !copy_data( entry, slot_base, &elf ) )
    return -4;

  /* Determine the task array index and set the stack pointer to the top (end) 
   * of the RAM partition, since the ARM stack grows downward.*/
  uint32_t *stack_top = (uint32_t *) ( slot_base + PROGRAM_SLOT_SIZE );

  /* Register the task in the kernel scheduler*/
  int task_id = task_program_create( free_slot,
                                     name,
                                     ( void ( * )( void ) )( slot_base + elf.entry_point ),
                                     stack_top,
                                     0,
                                     elf.code_size );

  return task_id;
}

/* Directory entry `index` of the FLASH image, or NULL when the image is
 * invalid or the index is outside the directory. The name is the first
 * IMG1_NAME_SIZE bytes. index is checked before the offset multiply, and
 * validation has already bounded that multiply by the image size. */
static const uint8_t *img1_entry_at( unsigned index )
{
  if ( !img1_validate( IMG1, IMG1_MAX_SIZE ) )
    return NULL;
  const uint8_t *bytes = (const uint8_t *) IMG1;
  uint32_t count = read_u32( bytes + 12 );
  if ( index >= count )
    return NULL;
  uint32_t directory_offset = read_u32( bytes + 16 );
  return bytes + directory_offset + (uint32_t) index * IMG1_ENTRY_SIZE;
}

int img1_count( void )
{
  if ( !img1_validate( IMG1, IMG1_MAX_SIZE ) )
    return -1;
  return (int) read_u32( (const uint8_t *) IMG1 + 12 );
}

int img1_name( unsigned index, char *name, unsigned capacity )
{
  /* capacity 0 has no room for the required NUL. A larger capacity is not
   * a directory field, so refuse it before any store. */
  if ( name == NULL || capacity < 1 || capacity > IMG1_NAME_SIZE )
    return -1;
  const uint8_t *entry = img1_entry_at( index );
  if ( entry == NULL )
    return -1;
  unsigned int length = 0;
  while ( length < IMG1_NAME_SIZE && entry[length] != 0 )
    ++length;
  /* A valid directory name always contains a NUL inside the field. */
  if ( length == 0 || length == IMG1_NAME_SIZE )
    return -1;
  /* length + 1 counts the NUL. It fits in the field, so the add cannot wrap. */
  unsigned int stored = length + 1;
  if ( stored > capacity )
    stored = capacity;
  for ( unsigned int i = 0; i + 1 < stored; ++i )
    name[i] = (char) entry[i];
  name[stored - 1] = 0;
  if ( stored != length + 1 )
    return -1;
  return 0;
}
