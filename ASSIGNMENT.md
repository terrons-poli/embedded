# Scratch-OS Project: Loading and Running Programs

## 1. What the project is about

In the scratch-OS we developed in class, every task is compiled and linked together with the
kernel. To add a new task, you must change the kernel and rebuild everything.
General purpose operating systems work differently: programs are separate files, and
the system loads one into memory only when a user asks to run it.

In this project you add this ability to scratch-OS. The goal is to implement a console that 
behaves as follows:

```text
scratch> ls
print_plain
thumb_ptr
scratch> run print_plain
loader: print_plain slot=0 base=0x20002000 id=4
print_plain: hello from a loaded program
program print_plain exited
scratch>
```

To get there, your team develops three things:

1. **A small shell.** It reads commands from the UART and lets the user list
   the available programs (`ls`) and start one (`run <name>`).
2. **A program loader in the kernel.** It finds a program in a read-only
   memory image, copies it into a free area of RAM, adjusts it so that it
   works at that address, protects it with the MPU, and starts it as a new
   task. When the program ends, or crashes, the loader frees the memory again.
3. **A host tool and a program build.** On your PC you compile each program
   separately from the kernel, and a Python tool packs all programs into one
   image file that QEMU places in FLASH to emulate and external disk.

To accommodate teams of different size, two versions of the project are available.

The rest of this document explains the ideas you need (section 2), which
version your team develops (section 3), what is given and what you write
(section 4), the requirements (section 5) and, at the end, the exact formats
and messages that every team must respect (section 6). The exact details
matter because one common test kit checks all teams automatically.

## 2. Ideas you need

### Why a loaded program must be "fixed"

When the linker builds a program, it decides the address of every function
and every global variable. For a program loaded at run time, nobody knows
this address in advance: it depends on which RAM area is free at that moment.
The program is usually linked as if it started at address `0`,
and the loader copies it from the image into a RAM slot before running it.

Most instructions do not care about memory location: a branch says "jump 40 bytes forward" and
works at any address. Some values, however, contain a complete address, for
example a pointer to a global variable or a table of function pointers. These
values are correct only for address `0`. When the loader copies the program to
address `0x20003000`, it must add `0x20003000` to each of them.

This adjustment is called **relocation**. The list of places that need it is
the **relocation list**. The linker can produce this list for you when you
link with the option `--emit-relocs`. The Cortex-M3 has no MMU (no virtual
memory), so there is no automatic way to make a program run at a different
address, you must "fix" the program.

### What a program contains

A program in memory has four parts:

- **code**: instructions and read-only constants (for example strings);
- **data**: global variables with an initial value, such as `int x = 5;`;
- **BSS**: global variables that start at zero, such as `int y;`. The file
  does not store these bytes; it stores only their size, and the loader fills
  the area with zeros;
- **stack**: used while the program runs.

### How a file says what it is: the magic number

A file is only a sequence of bytes; nothing in the bytes says "I am an
image" or "I am a program". For this reason most binary formats start with a
few fixed bytes, chosen by whoever defined the format, called the **magic
number** (or simply *magic*). A program that reads the file first compares
these bytes with the expected ones: if they differ, the file is not in that
format (or it is damaged) and is refused at once.

For example, every ELF file starts with the four bytes `7f 45 4c 46`
(`0x7f` followed by the letters `ELF`). This project requires to work with two
file formats named `IMG1` and `SFT1` (see later). The two file formats have
their own magic: an image starts with the four ASCII letters `IMG1`, and an
`SFT1` program file with the letters `SFT1`. Right after the magic, both formats
store a **version** number, so that the format can change in the future
without being confused with the old one. In this project the version is always `1`.

### Inside an ELF file

The GCC linker produces ELF files. You do not need to know the whole ELF
standard, only these parts:

- The **ELF header**, at the start of the file, contains the magic, the type
  of file, the entry point, and where the two tables below are.
- The **program header table** describes **segments**: the pieces of the file
  that must be placed in memory to run the program. A segment of type
  `PT_LOAD` is one that must be loaded.
- The **section header table** describes **sections**, the finer pieces the
  linker works with, such as `.text` (code), `.rodata` (constants), `.data`
  and `.bss`. A section is **allocated** when it takes space in memory while
  the program runs; debug information, for example, is not allocated. A
  section of type `SHT_NOBITS` takes memory but no bytes in the file: this is
  how BSS is stored.
- A **relocation section** (type `REL`) contains the relocation records for
  one other section; its field `sh_info` says which one. Each record gives a
  position (the *site*), a symbol and a **relocation type**, which says how
  the address must be computed. This project uses only three types:
  `R_ARM_ABS32`, a full 32-bit address and `R_ARM_THM_CALL` and
  `R_ARM_THM_JUMP24`, Thumb branches that are relative to the current
  position. Normally the linker removes these records from the final file;
  the option `--emit-relocs` asks it to keep them, so that the loader can use
  them.

You can see all these parts of your own programs with `arm-none-eabi-readelf`
(see the FAQ).

### Thumb addresses

The Cortex-M3 executes only Thumb instructions. In ARM code, the address of a
Thumb function has its lowest bit (bit 0) set to 1. When the loader relocates
a function pointer, adding the base address must keep this bit as it is.

### Memory protection and W^X

Loaded programs must run unprivileged.
The MPU must stop a program from touching the kernel or another program's
memory. The big version of the project also asks for the rule **W^X** ("write xor execute"):
a memory area may be writable or executable, never both. With W^X, a program
cannot overwrite its own code and cannot execute bytes stored in its data or
stack.

### Glossary

| Term | Meaning in this project |
|---|---|
| Slot | A fixed 4 KiB RAM area where one loaded program lives. There are four. |
| Magic number | Fixed bytes at the start of a file that identify its format. |
| Segment, section | Pieces of an ELF file: segments say what to load, sections are the linker's finer pieces (code, constants, data, BSS, debug). |
| Allocated section | A section that takes memory while the program runs. |
| Image, IMG1 | The read-only file in FLASH that contains all program files and a directory of their names. |
| SFT1 | The simple program-file format of the small version, produced by your host tool. |
| ELF | The standard file format produced by the GCC linker. |
| Relocation | A word in the program that holds an address and must be adjusted by the loader. |
| BSS | Global variables that start at zero and are not stored in the file. |
| W^X | A memory area is either writable or executable, not both. |
| Task id | The number that identifies a running loaded program. It combines the slot number with a counter (the *generation*) that grows each time the slot is reused, so an old id never matches a new program in the same slot. |

## 3. Which version your team develops

| Team size when the assignment is issued | Version |
|---:|---|
| 3 or 4 members | Small |
| 5 or 6 members | Big |

The version does not change later, even if the team changes size.

- **Small version.** Your host tool converts each ELF program into the simple
  SFT1 format. The kernel loads SFT1 files only and never reads ELF. One MPU
  region gives the program read, write and execute access to its whole slot.
- **Big version.** Everything required for the small version except the SFT1
  conversion, plus two extras: the kernel reads ELF files directly (the image
  stores them unchanged), and the MPU enforces W^X.

Both versions have the same maximum of 14 points. Big teams lose points if an
extra is missing (see the assessment section).

## 4. What is given and what you write

All teams start from `scratch-os/project-base/` and run on QEMU
`mps2-an385` (ARM Cortex-M3). Paths in this document refer to the published
scratch-OS repository unless stated otherwise. The base builds and boots with
`make run`, but it has no shell and no loader.

```text
on your PC, at build time                        on the board, in QEMU

programs/*.c -- compiler + program linker --> program files
                                                   FLASH 0x00000000
your host tool (mkimage.py) -- pack -------+       kernel + shell
                                            |
                                            +-----> FLASH 0x00200000
                                                   read-only fs.img (IMG1)
                                                          |
UART --> shell --> SVC --> find program --> free RAM slot |
                               |                          |
                               +-- copy, zero BSS, relocate
                               +-- set MPU and create task
                                                          |
                   unprivileged program <-----------------+
                   return / exit / fault --> free slot --> wake shell
```

**Given to you** (you must use it, not rewrite it): task records for four
loaded programs, task ids, waiting for a program and reporting how it ended,
the handling of program exit and of program faults, and system call 14. Its
interface is described in section 6.7.

**Written by you**: the shell, system calls 11 to 13, reading the image,
checking the program files, copying and relocating programs, the MPU settings
for a slot, the program startup code and linker script, the program build
rules, and the host tool `tools/mkimage.py`.

## 5. Requirements

The requirements below say *what* your system must do. They are not a work
plan: your team decides the order of work and how to split it. Each
requirement points to the section with the exact details.

### Requirements for both versions

**R1. Programs are built separately from the kernel.** You write the startup
code, the linker script and the build rules that compile each program in
`programs/` into its own ELF file, linked at address `0` with
`--emit-relocs`. A program shares no code with the kernel: each one includes
its own startup code, system-call stubs, `printf` and any compiler helper it
needs. `_start` calls `main`, and returning from `main` ends the program as
if it called `sys_exit()`. The build must create the programs and
`build/fs.img` from source, with ARM bare-metal GCC, every time in the same
way. Details: 6.3 and 6.5.

**R2. A host tool packs the programs into one image.** You write
`tools/mkimage.py`. It builds `build/fs.img`, in the IMG1 format, from a list
of programs, and it refuses wrong input with a clear error message. Small
teams also implement the conversion from ELF to SFT1. Details: 6.1, 6.2, 6.3.

**R3. The kernel reads the image in place.** The kernel finds the image at
FLASH address `0x00200000`, checks it, and looks up programs by name. It
reads the image where it is and never writes to it or copies it whole into
RAM. Details: 6.1.

**R4. A shell lets the user list and run programs.** The shell is a static
task that reads lines from the UART and accepts the commands `help`, `ls`
and `run <name>`. It prints exactly the prompt and messages given in 6.6, and
while a program runs it waits without printing a prompt.

**R5. Programs are started through system calls.** The shell uses system
calls 11 to 14 to count the programs, read their names, start a program and
wait for its end. They must return exactly the results listed in 6.5.

**R6. The loader places a program in a free slot and makes it work there.**
For `run <name>`, the kernel chooses the first free slot, copies code and
data into it, fills BSS with zeros, adds the slot address to every word in the
relocation list, checks the entry point and the stack, and only then creates
the task. Details: 6.2 (small) or 6.3 (big), and 6.4.

**R7. A program can touch only its own slot.** A loaded program always runs
from its RAM slot, where the loader copied it; the file in the image is only
the source of that copy. Programs run unprivileged, and the MPU lets a program
use its own slot and nothing else in RAM: no kernel RAM, no other slot. FLASH
stays readable and executable for all unprivileged tasks, as in the base,
because the shell is a static task whose code lives in FLASH; FLASH cannot be
written, so this does not weaken the protection. Details: 6.4.

**R8. The end of a program frees its slot.** When a program returns from
`main`, calls `sys_exit()`, or causes a fault that can be recovered, the
system prints the matching message, frees the slot at once, wakes the shell,
and the shell shows the prompt again. You obtain this behavior by using the
given code correctly (6.7), not by writing your own version.

**R9. Bad input never breaks the system.** A damaged image, a damaged
program file or a wrong system-call argument must be refused with the right
error, before any byte of the program is copied or executed. The kernel must
not crash, no slot may change, and the shell must keep working. Details: 6.1,
6.2, 6.3, 6.5.

### Extra requirements for the big version

**B1. The kernel loads ELF files.** The image contains each program ELF file
exactly as the linker produced it, and the host tool only packs files. The
kernel itself checks the ELF file, finds the parts to load and the
relocations, and applies them. Details: 6.3.

**B2. Code and data are protected separately (W^X).** Code can be read and
executed but not written. Data and stack can be read and written but not
executed. A program that writes into its code or jumps into its data stops
with a recoverable memory fault, as in R8. Details: 6.4.

## 6. Exact formats and messages

This section contains every detail that must be identical in all teams,
because the common test kit checks it byte by byte.

Unless stated otherwise, every number of more than one byte in IMG1 and SFT1
is an unsigned 32-bit little-endian value. Offsets count from the first byte
of the image or of the file. A range written `[start, end)` contains `start`
but not `end`.

### 6.1 The image (IMG1)

**Where it is.** QEMU loads `build/fs.img`, unchanged, at FLASH address
`0x00200000` with
`-device loader,file=build/fs.img,addr=0x00200000,force-raw=on`. The image may
use at most 2 MiB: `0x00200000..0x003fffff`. The kernel reads it there and
does not write to it or copy it.

**Header.** The image starts with a header of exactly 20 bytes:

| Bytes | Size | Field | Value |
|---:|---:|---|---|
| `0..3` | 4 | `magic` | magic number that identifies an image: bytes `49 4d 47 31` (`IMG1`), numeric value `0x31474d49` |
| `4..7` | 4 | `version` | exactly `1` |
| `8..11` | 4 | `image_size` | total size of the image, from 20 to `0x00200000` bytes |
| `12..15` | 4 | `count` | number of directory entries, from 1 to 32 |
| `16..19` | 4 | `directory_offset` | where the directory starts; a multiple of 4, at least 20 |

The value `directory_offset + count * 24` must not overflow and must not be
larger than `image_size`. Your packer always puts the directory at byte 20.

**Directory.** The directory has exactly `count` entries of 24 bytes each:

| Entry bytes | Size | Field | Value |
|---:|---:|---|---|
| `0..15` | 16 | `name` | program name, followed by a NUL byte |
| `16..19` | 4 | `offset` | where the program file starts; a multiple of 4 |
| `20..23` | 4 | `size` | exact size of the program file, at least 40 bytes |

A name has 1 to 15 characters, chosen from `A`-`Z`, `a`-`z`, `0`-`9`, `_` and
`-`. Two entries may not have the same name. The kernel ignores the bytes
after the first NUL; the packer sets them to zero and refuses duplicate
names.

**Program files.** `contents_start` is the end of the directory, rounded up to
a multiple of 4. Every file starts at or after `contents_start`, every range
`[offset, offset + size)` lies inside `image_size`, and no two files overlap.
Entries may be in any order, there may be gaps between files, and file sizes
need not be multiples of 4.

**What the packer must do.** It keeps the files in the order given on the
command line, aligns each start with zero padding, and sets `image_size` to the
end of the last file. It refuses zero files, more than 32 files, and images
larger than 2 MiB.

**What the kernel does with a bad image.** If any of these rules is broken,
the whole image is invalid. Then `ls` prints `error: no valid image`,
`run <name>` prints `error: program cannot be loaded`, and the shell keeps
working. A valid image may still contain a bad program file: `ls` lists it,
and `run` finds the problem when it checks the file.

### 6.2 The small-version program file (SFT1)

Small teams store programs in SFT1 files. Each file starts with a header of
exactly 40 bytes:

| Bytes | Size | Field | Value |
|---:|---:|---|---|
| `0..3` | 4 | `magic` | magic number that identifies an SFT1 file: bytes `53 46 54 31` (`SFT1`), numeric value `0x31544653` |
| `4..7` | 4 | `version` | exactly `1` |
| `8..11` | 4 | `header_size` | exactly 40 |
| `12..15` | 4 | `code_size` | size of code and constants; not zero, a multiple of 4 |
| `16..19` | 4 | `data_size` | size of initialized data; a multiple of 4 |
| `20..23` | 4 | `bss_size` | size of BSS; a multiple of 4; these bytes are not in the file |
| `24..27` | 4 | `entry` | entry point, counted from the start of code; bit 0 is 1 (Thumb) and `entry & ~1` is below `code_size` |
| `28..31` | 4 | `reloc_offset` | start of the relocation list; a multiple of 4 and exactly `40 + code_size + data_size` |
| `32..35` | 4 | `reloc_count` | number of entries in the relocation list |
| `36..39` | 4 | `file_size` | size of the whole file; equal to the directory-entry size and to the end of the relocation list |

**Layout.** Code and constants fill `[40, 40 + code_size)`. The next
`data_size` bytes hold the initialized data. Then comes the relocation list:
`reloc_count` words, each one the offset of a word to adjust. So
`file_size = reloc_offset + 4 * reloc_count`, and nothing follows the list.

**Limits.** `code_size + data_size + bss_size` must not overflow and must not
exceed 3072 bytes. `reloc_count` may be zero and is at most
`(code_size + data_size) / 4`. Every relocation offset is a multiple of 4,
appears only once, and points to a complete 4-byte word inside the code or the
data.

**How the loader uses it.** Before writing anything into the slot, the loader
checks every field, range, calculation and relocation, including each value
`slot_base + original_word`, which must not overflow 32 bits. Then it copies
code and data, fills BSS with zeros, and adds the slot base to each listed
word. The original word may point outside the program. Adding the base keeps
bit 0 of a Thumb function pointer.

**Errors.** Any SFT1 error gives the internal result `-4` and the message
`error: program cannot be loaded`. No program byte is copied or executed.

### 6.3 Program ELF files and the host tool

**What every program ELF looks like.** Programs are freestanding ARM EABI
Cortex-M3 Thumb executables, not kernel objects. They follow the normal C
calling convention, without a C library and without a heap. Each ELF is a
32-bit little-endian, version 1, ARM file of type `ET_EXEC`, linked at
address zero with `--emit-relocs`. Its records have the standard sizes:
52-byte file header, 32-byte program headers, 40-byte section headers,
16-byte symbols and 8-byte `REL` relocation records. At least one `PT_LOAD`
segment describes the part to load. The entry point is a Thumb address inside
the code. The ELF must not contain `RELA` records, dynamic linking,
undefined symbols or relocation types other than the ones accepted below for
loaded content. You decide the details of your Makefile.

**Which relocations matter.** Each relocation section says, in its `sh_info`
field, which section it applies to. Only relocations that apply to sections
that are loaded into memory ("allocated" sections) matter. Relocations for
other sections, such as debug information (including `R_ARM_NONE`), are
ignored. For loaded content:

- `R_ARM_ABS32` (a full 32-bit address) must be adjusted by adding the slot
  base;
- `R_ARM_THM_CALL` and `R_ARM_THM_JUMP24` (Thumb branches) are relative to the
  current position, so they are checked but need no change;
- every other type is refused.

**Small version: converting ELF to SFT1.** Your converter reads 52-byte ELF
headers, 32-byte program headers and 40-byte section headers. All tables must
be valid and inside the file, and there must be at least one `PT_LOAD`. The
lowest load address and the loaded content start at zero. Loaded sections do
not overlap and fit inside the loadable segments. Sections with initial
content come before all `SHT_NOBITS` (BSS) sections, and read-only sections
come before writable ones.

The converter fills gaps and alignment padding with zeros, rounds the ends of
code, initialized data and total memory up to a multiple of 4, and refuses
programs larger than 3072 bytes. It accepts only valid `REL` records that use
defined symbols. Word-aligned `R_ARM_ABS32` sites become the SFT1 relocation
list, sorted and without duplicates. Halfword-aligned `R_ARM_THM_CALL` and
`R_ARM_THM_JUMP24` sites are checked and produce no entry. The converter
refuses `RELA`, relocations in BSS, undefined symbols, invalid sites, and any
other relocation type for loaded content.

**Small version: host-tool commands.** The commands are exactly:

```text
python3 tools/mkimage.py flat PROGRAM.elf PROGRAM.sft
python3 tools/mkimage.py image build/fs.img NAME=PROGRAM.elf [NAME=PROGRAM.elf ...]
```

On success, `flat` prints `SFT1: ELF -> OUTPUT (N bytes)` and `image` prints
`IMG1: OUTPUT (N bytes, C programs)`. Before writing any output, the tool
refuses wrong arguments, names, ELF files and results that are too large. It
reports errors through Python `argparse`, which writes
`mkimage.py: error: <reason>` to standard error and exits with status 2.

**Big version: host-tool command.** The command is exactly:

```text
python3 tools/mkimage.py image build/fs.img NAME=PROGRAM.elf [NAME=PROGRAM.elf ...]
```

All IMG1 rules apply. The tool copies each ELF byte for byte, without changes.
Big teams do not need the `flat` command.

**Big version: what the kernel checks in an ELF.** The kernel checks the ELF
identity, the record sizes, and every program-header or section table it
uses. It checks the loadable segments and the loaded sections it needs to
find relocation targets, all `REL` sections, and the symbols used by the
relocations that matter. Before using any value, it checks that every
calculation, table, record, segment, section, relocation site and copied
range fits inside the file and the slot. It does not need to check section
names, debug or string contents, or symbol fields it does not use.

For loaded sections it applies `R_ARM_ABS32` by adding the slot base and
accepts resolved `R_ARM_THM_CALL` and `R_ARM_THM_JUMP24` without changes. It
ignores relocations for other sections, as long as their records fit in the
file. For any other type in a loaded section it first prints
`loader: unsupported relocation type <type>`, with the type as a decimal
number, and then the normal refusal message. A malformed ELF is refused
without a kernel fault.

### 6.4 Memory map and slots

Use the complete memory map of the base linker script. The kernel uses the
board's 4 MiB FLASH from `0x00000000`. The image starts at `0x00200000` and
must not go beyond `0x00400000`. The shell's RAM partition is
`0x20000000..0x20000fff`, and the idle task's partition is
`0x20001000..0x20001fff`. Kernel data and stack are placed outside them.

There are four fixed program slots of 4096 bytes, each aligned to 4096 bytes:

| Slot | Address range |
|---:|---|
| 0 | `0x20002000..0x20002fff` |
| 1 | `0x20003000..0x20003fff` |
| 2 | `0x20004000..0x20004fff` |
| 3 | `0x20005000..0x20005fff` |

**Choosing a slot.** The loader always takes the first free slot, in the
order 0, 1, 2, 3.

**Inside a slot.** Code, data, BSS and stack share the slot. The program
(code + data + BSS) uses at most the lower 3072 bytes. The upper 1024 bytes
hold the stack, which grows downward. The initial stack pointer is the end of
the slot, and the initial frames prepared by the given code use the top 64
bytes.

**Running and protection.** Programs run unprivileged in Thread mode, on the
process stack (PSP), under the existing preemptive round-robin scheduler. They
must not access kernel RAM or other slots. As in the base MPU setup, FLASH
`0x00000000..0x003fffff` can be read and executed by every unprivileged task;
the shell needs this, because its code is in FLASH, while a loaded program
executes only the copy in its slot. In the small version one MPU region gives
read, write and execute access to the current slot. In the big version the
regions must give: code read + execute, no write; data and stack read + write,
no execute (W^X).

### 6.5 System calls

**Calls that already exist.** Programs may link the stubs in
`lib/syscalls.s`. The kernel behaves as follows:

| SVC | C function | Result |
|---:|---|---|
| 1 | `int sys_write(const char *buf, unsigned len)` | number of bytes accepted, `0..len`; `-1` unless the whole byte range lies in the caller's partition or in FLASH |
| 2 | `int sys_read(void)` | next UART byte `0..255`, or `-1` when no byte is waiting |
| 3 | `uint32_t sys_uptime(void)` | milliseconds since the clock started |
| 4 | `void sys_exit(void)` | does not return; see below for loaded programs |
| 5 | start service, kernel only | a task receives `-1` |
| 6 | `void task_yield(void)` | asks for a task switch; the caller stays ready to run |
| 7 | `void task_delay(uint32_t ticks)` | blocks for the given number of 10 ms ticks; zero works like a yield |
| 8 | `int task_delay_until(uint32_t *last_wake, uint32_t period)` | `0`; `-1` unless the aligned 4-byte destination lies completely in the caller's partition |
| 9 | `void trace_dump(void)` | prints the existing scheduler trace |
| 10 | `uint32_t task_tick_count(void)` | ticks since the scheduler started |

The supplied `lib/printf.h` supports `%c`, `%s`, `%d`, `%u`, `%x` and `%%`,
and returns the number of characters printed.

**New loader calls.** Arguments are passed in the caller's registers `r0`,
`r1` and `r2`; the result is returned in `r0`. Any memory range that belongs
to the caller must lie completely in the caller's partition.

| SVC | C function | Result |
|---:|---|---|
| 11, `SYS_IMAGE_COUNT` | `int sys_image_count(void)` | number of directory entries, `1..32`; `-1` if the image is invalid |
| 12, `SYS_IMAGE_NAME` | `int sys_image_name(unsigned index, char *name, unsigned capacity)` | copies the complete name, with its final NUL, and returns `0`; `-1` if the image or the index is invalid, if `capacity` is not `1..16`, if the destination is outside the caller's partition, or if the name does not fit |
| 13, `SYS_PROGRAM_RUN` | `int sys_program_run(const char *name)` | the task id (a non-negative number); `-1` for an invalid name pointer or string or an invalid image, `-2` if the program is not found, `-3` if no slot is free, `-4` if the program file is invalid |
| 14, `SYS_PROGRAM_WAIT` | `int sys_program_wait(int task_id)` | waits for that running program, or returns at once the status it saved when it ended; `0` after return or exit, `-2` after a recovered fault, `-1` for zero, the caller's own id, or a malformed, unknown or old id |

More details:

- **SVC 12.** FLASH is not a valid destination. If `capacity` is too small,
  the call copies the first part of the name, still ending with NUL, and
  returns `-1`.
- **SVC 13.** `name` must be non-empty and end with NUL within 16 bytes, so it
  has at most 15 characters. Every byte read must lie in the caller's
  partition or in FLASH `0x00000000..0x003fffff`. The characters are not
  checked against the name rules; a name that cannot exist simply returns
  `-2`.
- **SVC 14.** There is no output pointer: when the program ends, the result is
  written into the saved `r0` of the waiting caller. A task id holds the slot
  number in bits 1:0 and a non-zero 28-bit generation counter above them, so
  the first ids are 4, 5, 6 and 7 for slots 0, 1, 2 and 3, and the next
  program in slot 0 gets id 8. Each slot keeps one end status until its next
  program ends. Waiting does not reserve a freed slot. Several tasks may wait
  for the same running program.
- **SVC 4.** It takes no arguments and does not return. For a loaded program
  it works like returning from `main`: it prints the exit message, saves
  status 0, wakes the waiting tasks, frees the slot and switches to another
  task. For a static task it ends the task, without a program message and
  without a reusable slot.
- **Unknown calls.** Unknown SVC numbers, SVC 0, and a task calling the
  kernel-only SVC 5 return `-1`.
- **SVC 1 with length 0.** It returns 0 without reading memory if the pointer
  is inside, or exactly at the edge of, the caller's partition or FLASH. Any
  other pointer returns `-1`. Range calculations must not overflow.

### 6.6 Shell input and output

**At boot** the kernel prints:

```text
scratch-os: kernel started
scheduler: round robin, quantum 5 ticks
```

**Reading a line.** The shell prints the prompt `scratch> ` (with a space,
without a newline). It echoes every stored character unchanged. A CR or an LF
prints one newline and ends the line. The shell stores and echoes at most 31
characters; further characters are dropped, without echo, until the line
ends. There is no line editing and spaces are not removed.

**Commands.** The only commands are exactly `help`, `ls`, and `run ` followed
by a non-empty name. A second space after `run ` is part of the name. An
empty line, `run` alone, extra characters after `help` or `ls`, and any other
input print `error: unknown command`.

`help` prints exactly:

```text
help - show commands
ls - list programs
run <name> - run a program
```

`ls` prints the program names in directory order, one per line. If the image
is invalid it prints:

```text
error: no valid image
```

When `run <name>` fails, the shell prints, for the results `-2`, `-3` and any
other negative value respectively:

```text
error: program not found
error: no free slot
error: program cannot be loaded
```

When `run <name>` succeeds, the kernel prints the following line, with
lowercase hexadecimal numbers and no leading zeros:

```text
loader: <name> slot=<slot> base=0x<base> id=<id>
```

While the program runs, the shell waits and prints no prompt. When the
program returns from `main` or calls SVC 4, the system prints:

```text
program <name> exited
```

When a loaded program causes a fault that can be recovered, the system
prints:

```text
<kind> in loaded program: CFSR=0x<cfsr>[ address=0x<mmfar>]
program <name> faulted
```

`<kind>` is exactly `MemManage`, `BusFault`, `UsageFault` or `HardFault`. The
part in square brackets appears only when the `MMARVALID` bit is set. The wait
returns `-2`, the slot is freed and the prompt comes back.

Some faults cannot be recovered: a MemManage or BusFault that happens while
the processor saves or restores the stack frame, a HardFault caused by such a
stack error, and every fault outside a loaded program. In these cases the
system prints the following line and stops, without another prompt:

```text
<kind>: unrecoverable CFSR=0x<cfsr> HFSR=0x<hfsr>
```

The prompt appears again after every other command, every failed load and
every recovered fault. Extra commands or debug output must never appear in,
delay, or change these transcripts.

### 6.7 Given code you must use

The base already contains four reusable task records (TCBs) for loaded
programs, task ids with generation counter, waiting and end-status records,
and the initial stack frames. It also handles return, exit and recovered
faults, wakes waiting tasks and implements SVC 14. Do not replace or copy this
code.

Your code provides the shell through `task_project_configure()` and
`task_configure_shell()`. It also provides SVC 11 to 13, the file checks,
loading, relocation, the entry and stack checks, and the MPU permissions. Call
`task_program_create(slot, name, entry, stack_top, executable)` only after a
program has been loaded completely and without errors.

The interface is declared in `kernel/task.h`:

| Function | What it does and how you use it |
|---|---|
| `task_project_configure(void)` | weak hook in the base; you write one normal (strong) definition and configure the shell there |
| `task_configure_shell(name, entry, stack_top, part_start, part_end, data_start, data_end, data_load)` | sets up the shell task; pass the addresses of the matching shell linker symbols |
| `task_program_slot_free(slot)` | tells whether a slot is free; false for an occupied or non-existent slot |
| `task_program_create(slot, name, entry, stack_top, executable)` | creates the task and returns its id, or `-1`; copies at most 15 name characters, builds the 64-byte initial frames, makes the task READY and prints the exact `loader:` line |
| `task_wait_program(id, saved_result)` and the SVC 14 code | waiting and end-status support; do not add another way of waiting through its kernel helper |
| `task_finish_current`, `task_fault_current`, `task_current_is_program`, fault entry and dispatcher | exit and fault-recovery support; do not replace or copy it |

The linker exports, as addresses, `_shell_part_start`, `_shell_stack_top`,
`_shell_part_end`, `_shell_data_start`, `_shell_data_end` and
`_shell_data_load`. It also exports `__program_slots_start`, each
`__program_slot_N_start` and `__program_slot_N_end`, `__program_slots_end`,
`__program_slot_size`, `__program_slot_count` and `__fs_image_start`. The
shell's writable sections come from `shell.o`; if you use another object name
you need equivalent linker rules. The shell's code entry and writable data
must stay inside its 4 KiB partition.

Big teams may add fields to the task records and extend the MPU setup done at
each task switch, so that code and data get different permissions. The given
behavior must stay the same. The `executable` argument supports the small
version's "whole slot" policy; on its own it does not give W^X.

**How a program's life goes.** SVC 13 checks all fields and relocations before
changing the slot, and then creates the task in state READY. The new program
may start running before its creator calls SVC 14. Waiting puts the caller in
state WAITING. Return, exit and a recovered fault save the end status, set the
task record directly to UNUSED, wake the waiting tasks and ask for a task
switch, so the slot can be reused at once. A failed load, or an attempt to
start a fifth program while four are running, must not change any slot. The
shell ignores the end status, because the kernel already prints how the
program ended.

## 7. Published tests

The [published test kit](test-kit/) is in the folder next to this document.
It contains `run_tests.py`, the test program sources, generators for broken
files, the suite definitions and the expected transcripts. Run it from its
folder, or from anywhere else with the right kernel and image paths.

Each test starts a fresh QEMU and compares everything printed on the UART
with the expected output. Only the host's CRLF line endings become LF; every
other byte must match exactly.

```text
python3 run_tests.py --suite small --kernel KERNEL.axf --image fs.img
python3 run_tests.py --suite big --kernel KERNEL.axf --image fs.img
```

Useful options: `--list` lists the tests, and `--test NAME` (repeatable) runs
only the named tests. Time limits for startup, each test and the whole run are
5, 5 and 90 seconds by default; change them with `--startup-timeout`,
`--test-timeout` and `--global-timeout`. The exit status is 0 when the suite
passes, 1 for failed tests or QEMU problems, and 2 for configuration errors.
QEMU stops after each result. Each test prints `PASS <name>` or
`FAIL <name>: <reason>`, and the run ends with
`SUMMARY suite=<suite> passed=<passed> failed=<failed> total=<total>`.

The small suite has 10 tests. The big suite runs the same ten tests with ELF
files and adds four big-only tests:

| Test | What it checks |
|---|---|
| `nonzero_slot_relocation` | A parent program runs in slot 0 (base `0x20002000`, id 4). Its child runs in slot 1 (base `0x20003000`, id 5) and prints `thumb_function_pointer: thumb=1 result=42`. The parent receives 0. |
| `global_state` | Slot 0, id 4: prints `globals_bss_const: global=37 bss=0 table=16 sum=53`, then exits. |
| `thumb_ptr` | Slot 0, id 4: prints `thumb_function_pointer: thumb=1 result=42`, then exits. |
| `normal_return` | Returns from `main`: `normal_return: returning from main`, then `program normal_return exited`. |
| `explicit_exit` | Calls SVC 4: `explicit_exit: calling sys_exit`, then `program explicit_exit exited`. |
| `kernel_store` | Writes to `0x20000000`: `MemManage in loaded program: CFSR=0x82 address=0x20000000`. Then `print_plain` reuses slot 0 with id 8. |
| `bad_magic` | Refused; `help` then shows that the shell still works. |
| `bad_header` | Refused; `help` then shows that the shell still works. |
| `too_large` | A 3076-byte program is refused; `help` then shows that the shell still works. |
| `bad_reloc` | A relocation outside the program is refused; `help` then shows that the shell still works. |
| `unsupported_reloc` (big only) | Type 3 prints `loader: unsupported relocation type 3`. A good program then runs in slot 0 with id 4. |
| `truncated_elf` (big only) | A cut ELF file is refused without any fault line. A good program then runs in slot 0 with id 4. |
| `wx_write_code` (big only) | A write into code prints `MemManage in loaded program: CFSR=0x82 address=0x20002280`. A good program then reuses slot 0 with id 8. |
| `wx_exec_data` (big only) | A jump into data prints `MemManage in loaded program: CFSR=0x1`. A good program then reuses slot 0 with id 8. |

The expected transcripts also contain both boot lines, the echoed commands,
the loader and end lines, and every `scratch> ` prompt. The whole transcript
must match. The assessment uses the same kit.

## Assessment: 14 points

| Part | What is assessed | Maximum |
|---|---|---:|
| Team | The image: valid IMG1 built, directory checked, programs found without copying the image (R2, R3). | 1.00 |
| Team | Shell and image/run system calls: exact behavior, errors, output, and return to the prompt (R4, R5). | 1.00 |
| Team | Program build: startup code, linker script, library and system-call integration, host-tool behavior (R1, R2). | 0.75 |
| Team | Loading and relocation: copy data, zero BSS, check entry and stack, adjust addresses, then create the task (R6). | 2.50 |
| Team | MPU isolation and correct use of the given run, wait, exit, fault, free and reuse support (R7, R8). | 1.00 |
| Team | Refusing unsafe or malformed input before execution, without changing a slot or crashing (R9). | 0.75 |
| | **Requirements subtotal** | **7.00** |
| Team | Passing the published test suite of your version, proportionally (see below). | 1.50 |
| Team | Efficiency: limited resources, no unneeded image copy or dynamic allocation, clear checks, no avoidable duplication. | 0.50 |
| Team | Documentation: build and run instructions, design choices, limits, and enough detail to reproduce the result. | 0.75 |
| Team | Presentation and demo: explain the system and show loading, relocation, refusal of bad input and fault recovery. | 1.25 |
| Team | Teamwork. | 0.00 |
| | **Team subtotal, the same for every member** | **11.00** |
| Individual | Explain any part chosen by the instructor, including parts written by teammates. Questions may cover design, formats, relocation, MPU, checks, system calls and the life of a program. | 3.00 |
| | **Individual subtotal, assessed for each student** | **3.00** |
| | **Maximum raw score** | **14.00** |

Rows assessed by quality use steps of 0.25 points. The test row is not
rounded:

```text
test points = 1.50 * passed applicable tests / applicable suite tests
```

The number of applicable tests is 10 for small teams and 14 for big teams.
Teamwork earns zero points. Individual points do not depend on who declares
to have written which part.

For a big team, first compute the raw score from 0 to 14. Then subtract each
applicable penalty once. A negative result becomes zero:

| Big-version extra missing or not working | Penalty |
|---|---:|
| B1, ELF loading in the kernel: byte-for-byte packing, the required relocation handling, the numeric type message, and the range checks for every table, record and range used | -2.00 |
| B2, W^X protection of code and data, in both directions | -1.00 |

```text
final score = max(0, raw score - applicable big-version penalties)
```

ELF loading is one extra and can cause one penalty only. W^X is also one
extra: a missing direction does not count as a second penalty.

## Rules and logistics

- Start from `scratch-os/project-base/`. Do not replace the given program
  life-cycle and fault-recovery code.
- Every member must understand the whole project, including the code written
  by teammates. The instructor may ask any member about any part, and each
  answer counts for that student's score.
- The live presentation must show normal loading, relocation in a slot other
  than 0, refusal of bad input, fault recovery, and the behavior specific to
  your version.
- Submit all source and build files needed to rebuild the kernel and all
  programs, plus `tools/mkimage.py` and `build/fs.img`. The test runner uses
  `build/scratch-os.axf` as the kernel. Your documentation must list the
  prerequisites, the exact build, run and test commands, your design and
  format choices, how you integrated the MPU and the given life-cycle code,
  known limits, and the exact demo procedure. Generated object files are not
  part of the delivery, except the required image and the runnable kernel.
- The assessment uses the state of your repository at the deadline and the
  test suite of your version. Your own extra programs or tests must not change
  the fixed commands, output, formats or test behavior.
- There is no required start date.
- You must complete the project by the end of the academic year.
- Each team receives course GitLab accounts. That repository is where you must
  keep and submit your work.

## What is fixed and what you decide

| Fixed for all teams | Your team decides |
|---|---|
| QEMU board and CPU, image address and size limit, IMG1 format and limits | How to organize modules and files |
| Version assigned by team size | How members organize and coordinate the work |
| Slot addresses, size and alignment, first-free choice, 3072-byte program limit, 1024-byte stack | Helper functions for checks and internal data structures, as long as calculations and ranges are safe |
| System-call numbers, arguments, results, pointer rules, task ids, program life cycle | Kernel function names, except the given integration points |
| Shell commands, prompt, output, input limit, error messages | How the shell is structured and how it buffers input |
| Program build rules: linking at address zero, Thumb entry, `--emit-relocs`, return from `main` acting as exit | Extra private test programs and host-side tests |
| Small: SFT1 format, conversion and relocation rules, one read/write/execute MPU region | Structure of the host-side ELF reader (no third-party library is needed) |
| Big: ELF stored unchanged, required ELF subset, relocations and checks, numeric type message | Structure of the kernel ELF reader; content that is not used need not be checked |
| Big: code read + execute; data and stack read + write, not executable | Which MPU regions give these permissions |
| Given life-cycle code and calls | Style of documentation and presentation |
| Test suite and transcripts, points, penalties | Extra features that do not change any fixed behavior |

## FAQ and pitfalls

### How do I inspect and debug a loaded program?

Use the ARM bare-metal tools `readelf`, `objdump`, `nm` and `size` to see the
headers, the entry point, the loaded sections and the relocations.
`build/output.map` shows the kernel linker symbols. GDB does not know where a
program was loaded, so tell it the slot address with
`add-symbol-file PROGRAM.elf LOAD_ADDRESS`, for example
`add-symbol-file build/programs/thumb_ptr.elf 0x20003000`. Use the RAM slot
address, not the address of the file in the image.

### What can go wrong with MPU regions?

On the Cortex-M3, an MPU region's size is a power of two, and its base address
must be aligned to that size. When regions overlap, the one with the higher
number wins. Check the resulting permissions after every task switch. In the
big version, code must refuse writes, and data and stack must refuse
instruction fetches; an overlapping region with read/write/execute access
breaks W^X.

### What may I change in the given base?

You may add modules and add fields to the system-call, task and MPU code. The
given life-cycle and fault code must stay in control. Faults while saving the
stack frame, faults in the kernel, and faults outside loaded programs must
remain unrecoverable.

The `make image` recipe in the base is only a starting point: change it to use
the exact `image` command with explicit `NAME=PROGRAM.elf` arguments.
`make run` uses an existing image, so your build must create a real IMG1
first. The placeholder image in the base is deliberately not a valid IMG1.
