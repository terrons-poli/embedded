# Programs

Each `programs/NAME.c` is a loadable program. The build compiles it on its own
into `build/programs/NAME.elf`, linked at address 0 with `--emit-relocs` by
`scripts/programs.ld`. Every program gets its own copy of the runtime: the
startup code in `runtime/start.s`, plus `lib/syscalls.s`, `lib/printf.c` and
`lib/format.c`. `_start` calls `main`, then `sys_exit`, so returning from
`main` ends the program.

`make image` builds every program and packs them into `build/fs.img`:

```text
python3 tools/mkimage.py image build/fs.img NAME=build/programs/NAME.elf ...
```

`make programs` builds the ELF files without packing them.

The file name, without `.c`, becomes the program's IMG1 name, so it must have
1 to 15 characters, using only letters, digits, `_` and `-`. Only `.c` files
directly in this folder are programs; `runtime/` holds the shared startup
code.
