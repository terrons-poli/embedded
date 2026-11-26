# The `mkimage.py` host tool

## Overview

`mkimage.py` runs on the development machine and bundles compiled user
programs into an IMG1 image. An IMG1 image is a binary file with a directory
that tells the kernel where to find each program by name. The Makefile builds
`build/fs.img`, and QEMU places it at Flash address `0x00200000` when you run
`make run`. [Host tool](../tools/mkimage.py) [Makefile](../Makefile)

## How it works

`make image` builds the kernel and compiles each `programs/NAME.c` into an
ELF executable at `build/programs/NAME.elf`. Make passes each executable to
the tool as `NAME=FILE` and writes the result to `build/fs.img`. To choose the
names and files yourself, run the tool from the repository root with an output
path followed by one or more `NAME=FILE` arguments. This example packs
`test.elf` under the name `demo` in its own image.

```sh
make programs
python3 tools/mkimage.py image build/examples/demo.img demo=build/programs/test.elf
```

The tool splits each argument at the first `=` and reads the file on the
right. The name on the left becomes the directory entry. It accepts 1 to 32
programs with unique, case-sensitive names. Each name must be 1 to 15 ASCII
letters, digits, underscores or hyphens. Each file passed to the command must
be at least 52 bytes, enough room for the header of a 32-bit ELF executable.
The finished image must fit in 2 MiB, including the header, directory and
padding.

The packer writes the parts below in order. All numeric fields use four bytes
in little-endian order, with the least significant byte first.

| Part | Contents |
| --- | --- |
| 20-byte header | The four bytes `IMG1`, version `1`, total image size, program count and directory offset `20`. |
| 24-byte directory entry per program | A 16-byte name field padded with zero bytes, the program's offset from the start of the image and its exact file size. |
| Program contents | The original file bytes in command-line order, with zero bytes before each file as needed for four-byte alignment. |

The image ends at the end of the last program. The tool builds it in memory,
creates the output's parent directory if needed and writes the completed
file. On success it prints `IMG1: OUTPUT (N bytes, C programs)`, where
`N` is the image size and `C` is the program count. Invalid input, unreadable
files and output errors produce `mkimage.py: error: <reason>` on standard
error and exit with status 2. [IMG1 format in the assignment](../ASSIGNMENT.md#61-the-image-img1)

## Code details

### The packer keeps the ELF bytes intact

`parse_args()` calls `validate_inputs()` after reading the files.
`pack_into_img()` also validates the input, so direct Python callers get the
IMG1 checks even when they bypass the command line. Both use the same helper
with a different minimum file size. [Validation and packing](../tools/mkimage.py#L64)

```python
_validate_inputs(programs_tuples, ELF32_HEADER_SIZE)
```

The line above is the command's size check. The packer itself accepts
payloads of 40 bytes or more, which is the IMG1 format's minimum. Both
checks look at the file length. When the shell runs a named program, the
kernel locates that entry in Flash and validates the ELF before copying
its loadable parts into RAM and adjusting addresses for the chosen RAM slot.
Parsing ELF on the host could catch more errors during the build. It would
add a second parser, since the kernel can receive images from other sources
and needs its own checks.
[Kernel loader](svc13-loader.md)

### Each program starts on a four-byte boundary

Inside `pack_into_img()`, `base_offset` is the end of the directory.
`len(contents)` includes the files and padding already added. The packer
uses their sum to find the next available byte. [Alignment code](../tools/mkimage.py#L100)

```python
current_offset = base_offset + len(contents)
padding_size = (-current_offset) % 4
contents += bytes(padding_size)
offset = current_offset + padding_size
directory += _encode_int(offset)
```

With two entries, the first program starts at byte `20 + 2 * 24 = 68`.
If that file is 53 bytes long, the next available byte is 121. Python's
`(-121) % 4` gives 3, so the packer inserts three zero bytes and records
124 as the second program's offset. The first program's directory entry
still records a size of 53. The kernel uses the offset to find an aligned
start for the ELF and the size to check which bytes belong to it.

### A failed write keeps the previous image

`_write_atomically()` creates a temporary file beside the destination, writes
the image there and closes it before replacing the destination.
[Output writer](../tools/mkimage.py#L174)

```python
with os.fdopen(fd, "wb") as tmp:
    tmp.write(data)
    # mkstemp creates the file as 0600; use the usual mode for outputs.
    os.fchmod(tmp.fileno(), 0o644)
os.replace(tmp_name, output)
```

Writing directly to the destination would use less disk space during the
update, but opening an existing image for writing would truncate it before
the new bytes were ready. Using a temporary file in the same directory lets
`os.replace()` do the switch in one operation on that filesystem. For
example, if the disk fills during the write, the tool removes the temporary
file and the old image is still available.
