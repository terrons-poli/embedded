#include "kernel/loader.h"
#include <stdint.h>

#define IMG1_MAX_SIZE ( 1UL << 21 )
#define MAX_NAME_SIZE ( 15 )

static const char magic_bytes[] = "IMG1";

static unsigned int validate_program_name( const char *name )
{
	if ( !name ) return 0;

	unsigned i = 0;
	for (; name[i] != 0 && i < MAX_NAME_SIZE; ++i )
	{
		if ( 
				( name[i] >= 'A' && name[i] <= 'Z' ) ||
				( name[i] >= 'a' && name[i] <= 'z' ) ||
				( name[i] >= '0' && name[i] <= '9' ) ||
				name[i] == '-' || name[i] == '_'
		) continue;

		return 0;
	}
	
	if ( name[i] != 0 ) return 0;

	return 1;
}

static unsigned int img1_validate_header()
{
	for ( unsigned i = 0; i < 4; ++i )
		if ( IMG1->magic[i] != magic_bytes[i] )
		{
			//TODO: add kprintf with error
			return 0;
		}

	if ( IMG1->version != 1 )
	{
		//TODO: add kprintf with error
		return 0;
	}

	if ( IMG1->image_size > IMG1_MAX_SIZE)
	{
		//TODO: add kprintf with error
		return 0;
	}

	if ( IMG1->count == 0 || IMG1->count > 32 )
	{
		//TODO: add kprintf with error
		return 0;
	}

	for ( unsigned i = 0; i < IMG1->count; ++i )
	{
		const uint8_t *image_ptr = (const uint8_t*)IMG1;
		const struct img1_directory_entry* de = (const struct img1_directory_entry*)(image_ptr 
				+ sizeof(struct img1_header) 
				+ sizeof(struct img1_directory_entry) * i);

		if ( !validate_program_name(de->name) )
		{
			//TODO: add kprintf with error
			return 0;
		}

		//TODO: make more checks
	}

	return 1;
}
