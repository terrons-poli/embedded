/* ENTRY(_start) writes address of symbol "_start" in ELF's e_entry. 
 * Kernel will use it as a entry point.
*/
ENTRY(_start)

/* Offsets within a program slot, linked at address 0.
 * Code, data and BSS use the lower 3 KiB; the upper 1 KiB is the
 * downward-growing stack. The loader adds the slot base to the stack
 * top and passes it to the kernel, which prepares the initial frames.
 * Symbols describe this space without adding it to a PT_LOAD segment.
 */
PROGRAM_SLOT_SIZE = 4096;
PROGRAM_STACK_SIZE = 1024;
__stack_top = PROGRAM_SLOT_SIZE;
__stack_limit = __stack_top - PROGRAM_STACK_SIZE;


/* PHDRS defines ELF segments (program header). MPU will use them for permissions */
PHDRS {
    text PT_LOAD FLAGS(5);  /* R + X */
    data PT_LOAD FLAGS(6);  /* R + W */
}

SECTIONS {
    . = 0;
    .text : {
        KEEP(*(.text._start))       /*  Startup first  */
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } : text                        /*  goes in "text (R+X)" defined in PHDRS  */ 


    . = ALIGN(4);               /* TO DO: change alignement according to mpu settings (github issue #4) */
    .data : {
        *(.data .data.*)
        . = ALIGN(4);
    } : data

    .bss (NOLOAD) : {
        . = ALIGN(4);
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
    } : data

    /DISCARD/ : {
        *(.ARM.exidx* .ARM.attributes .comment)
    }

    ASSERT(. <= __stack_limit, "program overlaps the reserved 1 KiB stack")
}

