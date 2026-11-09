#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include <elf.h>

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

struct elf_image{
	const Elf32_Ehdr *ehdr; /* elf header */
	const Elf32_Phdr *phdrs; /*program headers*/
	uint16_t ph_count;
	const ElF32_Shdr *shdrs; /* section headers*/
	uint16_t sh_count;
	const Elf32_Sym *sym; /* string and symbol tables*/
	uint16_t sym_count;
	uint32_t entry_point; /*e_entry*/
}

int load_program(const uint8_t *name);
#endif // 
