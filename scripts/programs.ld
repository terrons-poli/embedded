/* ENTRY(_start) writes address of symbol "_start" in ELF's e_entry. 
 * Kernel will use it as a entry point.
*/
ENTRY(_start)


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
}

