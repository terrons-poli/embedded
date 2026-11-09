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

static unsigned int validate_elf(const Elf32_Ehdr *ehdr, uint32_t file_size, struct elf_image *elf){
	
	const uint8_t *elf_raw = (const uint8_t *) ehdr;

	if(file_size < sizeof(Elf32_Ehdr))
		return 0;

	/*Header validation*/
	if(	ehdr->e_ident[EI_MAG0] != ELFMAG0 || 
			ehdr->e_ident[EI_MAG1] != ELFMAG1 ||		/*Magic bytes*/
			ehdr->e_ident[EI_MAG2] != ELFMAG2 || 
			ehdr->e_ident[EI_MAG3] != ELFMAG3 ||
			ehdr->e_ident[EI_CLASS] != ELFCLASS32	||												/*Elf 32 bit*/
			ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||												/*Little-endian*/
			ehdr->e_ident[EI_VERSION] != EV_CURRENT	||											/*Version 1*/
			ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_ARM )					/*Executable ARM elf*/
		return 0;
	
	/*Size validation*/
	if(ehdr->e_ehsize != 52 || ehdr->e_phentsize != 32 || ehdr->e_shentsize != 40 || ehdr->e_shnum == 0 || ehdr->e_phnum == 0)
		return 0;
	
	
	uint32_t ph_table_end = ehdr->e_phoff + ((uint32_t)ehdr->e_phnum * 32u);
	uint32_t sh_table_end = ehdr->e_shoff + ((uint32_t)ehdr->e_shnum * 40u);

	if(ph_table_end > file_size || sh_table_end > file_size)
		return 0;
	
	const Elf32_Shdr *shdrs = (const Elf32_Shdr *) (elf_raw + ehdr->e_shoff);
	const uint16_t shdrcount = ehdr->e_shnum;
	const Elf32_Sym *symtable = NULL;
	uint32_t symcount = 0;
	/*Check Symbols size and relocation records, it must not have RELA records, dynamic linking, undefined symbols or relocation types other than the ones accepted for loaded content*/

	for(uint16_t i=0; i<shdrcount; i++){
		if(	shdrs[i].sh_type == SHT_RELA ||	/*RELA section*/
				shdrs[i].sh_type == SHT_DYNAMIC) /*Dynamic linking section*/
			return 0;

		if( shdrs[i].sh_type == SHT_SYMTAB ){ /* Check symbol entries size*/
			if( shdrs[i].sh_entsize != 16 )
				return 0;
			symtable = (const Elf32_Sym *)(elf_raw + shdrs[i].sh_offset);
			symcount = symtable->sh_size/16;
		}
		if( shdrs[i].sh_type != SHT_NOBITS ){
			if(shdrs[i].sh_offset + shdrs[i].sh_size > file_size)
				return 0;
		}
	}
	
	/*Check relocation sections*/

	for(uint16_t i = 0; i < shdrcount; i++){
		if(shdrs[i].sh_type != SHT_REL)
			continue;
		
		if(shdrs[i].sh_entsize != 8)
			return 0;

		if(shdrs[i].sh_info >= shdrcount)
			return 0;
		
		/*Check relocation target*/
		const Elf32_Shdr *target = &shdrs[shdrs[i].sh_info];

		if(!(target->sh_flags & SHF_ALLOC))
			continue;
		
		if(!symtable)
			return 0;
		
		/*Check relocation section*/
		const Elf32_Rel *rels = (const Elf32_Rel *)(elf_raw + shdr[i].sh_offset);
		uint32_t relcount = shdrs[i].sh_size / 8;

		for(uint32_t r = 0; r<relcount; r++){
			uint32_t r_type = ELF32_R_TYPE(rels[r].r_info);
			uint32_t r_sym = ELF32_R_SYM(rels[r].r_info);

			if(r_type != R_ARM_ABS32 &&
					r_type != R_ARM_THM_CALL &&
					r_type != R_ARM_THM_JUMP24)
				return 0;
			
			if(rels[r].r_offset + 4 > target->sh_size) /* The patched target field is 4 bytes wide */
				return 0;

			if(r_sym >= symcount)
				return 0;

			if(symtable[r_sym].st_shndx == SHN_UNDEF) // The symbol must exist in the table
				return 0;
		}
	}
		
			
	/*Program headers validation*/
	const Elf32_Phdr *phdrs = (const Elf32_Phdr *)(elf_raw + ehdr->e_phoff);
	const uint16_t phdrcount = ehdr->e_phnum;
	int loadcount = 0;
	for(uint16_t i = 0; i < phdrcount; i++){
		if(phdrs[i].p_type == PT_LOAD){
			loadcount++;
			/*The segment must be smaller than file_size*/
			if(phdrs[i].p_offset + phdrs[i].p_filesz > file_size || phdrs[i].p_memsz < phdrs[i].p_filesz)
				return 0;
		}
		/*No dynamic links*/
		if(phdrs[i].p_type == PT_DYNAMIC || phdrs[i].p_type == PT_INTERP)
			return 0;
	}

	if(loadcount == 0){
		return 0;
	}
	
	if((ehdr->e_entry & 1u) == 0)	/*Thumb entry point*/
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

static unsigned int search_elf(const struct img1_directory_entry *directory, struct elf_image *elf){
	if(!directory)
		return 0;
	const uint8_t *base = (const uint8_t *)IMG1 + directory->offset;
	const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *) base;
	
	return validate_elf(ehdr,directory->size,elf);
	

}

static const int copy_data(const struct img1_directory_entry *entry, const int slot){
	/*TODO: Search ELF file with header and sections to check it*/
	
	

	
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
