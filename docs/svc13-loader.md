# SVC 13: loading a program safely

`sys_program_run(name)` checks caller memory in `kernel/syscall.c`, then
passes a bounded copy of the string to `load_program()` in `kernel/loader.c`.
A name can contain any characters: `foo/bar` is a valid lookup string that
returns `-2` because IMG1 cannot contain that name. Directory entries still
use the stricter IMG1 character rules.

## Validate before changing RAM

The loader checks every used ELF range before copying segments or applying
relocations. It uses `offset <= limit && length <= limit - offset`, rather
than adding an unchecked length. For example, a section-table offset near
`UINT32_MAX` must not wrap its calculated end into the file.

Program headers are checked first. The entry must be a Thumb address within
file-backed executable bytes. Allocated sections must fit both the lower
3072 bytes of the slot and a loadable segment. For file-backed sections,
the section's file offset must agree with the segment's mapping.

A relocation then has to fit completely inside its allocated target section.
The section must have initialized contents, the site must have the required
alignment, and its symbol must be defined in the table selected by that REL
section's `sh_link`. Duplicate ABS32 sites are rejected so that a word cannot
have the slot base added twice. Debug relocations are ignored after their
record ranges and target indexes have been checked.

These checks use program-relative addresses. They need no slot base: an
ABS32 site at offset 4096 is outside the program area in every slot.

The parser keeps typed ELF records and rejects unaligned table offsets before
casting. Decoding every record byte by byte would allow a more general parser,
but adds complexity that is unnecessary for the word-aligned ARM ELF files
produced by this project's linker.

## Caller strings and results

A full 16-byte range check would reject a short valid string that ends at an
allowed memory boundary. Instead, the syscall checks and copies one byte at
a time, stopping at the first NUL. It refuses an empty name, an unreadable
byte, or a name without a NUL in the first 16 bytes. The private copy also
prevents the loader from continuing to read caller-owned memory.

| Result | Meaning |
|---|---|
| Non-negative | Created task id |
| `-1` | Invalid name pointer/string or invalid IMG1 image |
| `-2` | Program not found |
| `-3` | No free slot |
| `-4` | Invalid program file |

## Validation and scope

Use the unchanged professor test kit with `make test`. It covers normal
loading, relocation in another slot, malformed files and fault recovery.

MPU permissions and W^X are handled by a separate PR. This change does not
alter the MPU, task protection policy, or program linker layout. Consequently,
the two W^X tests in `make test` remain failing until that work is integrated.
