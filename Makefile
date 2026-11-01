# Embedded Operating Systems and Architecture course
# Licensed under the MIT License. See LICENSE in the repository root.
#
# Contact: Stefano Di Carlo <stefano.dicarlo@polito.it>

# ARM bare-metal cross-compiler: runs on the host, emits Cortex-M code.
CC := arm-none-eabi-gcc

# Match the clang-format major version used by CI.
CLANG_FORMAT ?= clang-format-18

# The time slice of the round-robin scheduler, in ticks (one tick =
# 10 ms). Change it from the command line: `make QUANTUM=2 run`.
QUANTUM ?= 5

# All generated files go under build/; `make clean` just deletes it.
BUILD_DIR := build
# Final linked image. .axf is ARM's name for an ELF carrying debug info.
BIN := $(BUILD_DIR)/scratch-os.axf
IMAGE := $(BUILD_DIR)/fs.img
IMAGE_ADDR := 0x00200000
IMAGE_TOOL ?= tools/mkimage.py
PROGRAM_DIR ?= programs

PROG_BUILD_DIR := $(BUILD_DIR)/programs
# wildcard to avoid make to enter in subdirectories of PROGRAM_DIR
PROG_SOURCES := $(wildcard $(PROGRAM_DIR)/*.c)
# notdir only takes the filename, basename excludes the extension
PROG_NAMES := $(basename $(notdir $(PROG_SOURCES)))
# Takes PROG_NAMES and makes PROG_BUILD_DIR/PROG_NAMES.elf
PROG_ELFS := $(patsubst %, $(PROG_BUILD_DIR)/%.elf, $(PROG_NAMES))

# Every program links its own copy of these (R1: no code shared with the kernel)
PROG_RUNTIME := $(PROGRAM_DIR)/runtime/start.s lib/syscalls.s lib/printf.c lib/format.c
PROG_RUNTIME_OBJS := $(patsubst %, $(PROG_BUILD_DIR)/obj/%.o, $(basename $(PROG_RUNTIME)))

# The folders that hold our source files, one folder per OS layer.
SRC_DIRS := boot kernel drivers lib src
# Find every .c file (C code) and every .s file (assembly) in those
# folders. `wildcard` is make's way of listing files that match a pattern,
# so a new file is built automatically, with no change to this Makefile.
SOURCES := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c) $(wildcard $(d)/*.s))
# Map each foo.c or foo.s to its build/foo.o object file. The object
# keeps the folder name, e.g. kernel/irq.s becomes build/kernel/irq.o.
# Two nested patsubst calls: the inner one rewrites ".s" sources, the
# outer one rewrites ".c" sources (a word that does not match is left
# alone, so each source is changed by the one that applies to it).
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(patsubst %.s,$(BUILD_DIR)/%.o,$(SOURCES)))

# -mthumb / -mcpu: target the Cortex-M3's Thumb instruction set.
# -nostartfiles: no default C startup code - boot/startup.s is ours.
# -ffreestanding: no hosted environment - no full standard library, no
#  special meaning for main. Together with -nostdlib below, this makes
#  "no C library" true of the image.
# -std=gnu17: C17 plus GNU extensions (used by the inline-asm blocks).
CFLAGS := -mthumb -mcpu=cortex-m3 -nostartfiles -ffreestanding -std=gnu17
# -Iinclude: look for #include "..." headers in the include/ folder,
# so every file can write #include "cmsdk.h" whatever its own folder.
CFLAGS += -Iinclude
# -I.: also look in the project folder itself (the one holding this
# Makefile). Every other header is then written with its folder, e.g.
# #include "drivers/uart.h", the same from whatever file includes it.
CFLAGS += -I.
# -fno-builtin: GCC normally treats names like printf as the C library
# function it knows. There is no C library here (-nostdlib): our own
# printf in lib/printf.c is a plain function like any other.
CFLAGS += -fno-builtin
# Enable the standard and extra warning sets.
CFLAGS += -Wall -Wextra
# -O0 + full debug info: keep the generated code close to the source so
# it can be stepped through instruction by instruction in class.
CFLAGS += -O0 -g3
# Not -Werror: this code is kept across course years, and a new compiler
# warning should not stop the build.

# Pass the quantum to the C code: -DQUANTUM=5 defines QUANTUM as 5,
# as if kernel/task.h had `#define QUANTUM 5`.
CFLAGS += -DQUANTUM=$(QUANTUM)

# -I. for example #include "lib/printf.h"
PROG_CFLAGS := -mthumb -mcpu=cortex-m3 -ffreestanding -std=gnu17 -I. -fno-builtin
PROG_CFLAGS += -Wall -Wextra -O0 -g3

# Use our linker script instead of the toolchain's default.
LDFLAGS := -T scripts/mps2_m3.ld
# Also write a map file: it shows which symbol landed at which address.
LDFLAGS += -Xlinker -Map=$(BUILD_DIR)/output.map
# -nostdlib: do not link a C library or its startup code.
# -lgcc: still link libgcc, the small compiler helper library (for
# example software integer division, which the compiler may use alone).
LDFLAGS += -nostdlib -lgcc

PROG_LDFLAGS := -T scripts/programs.ld -Wl,--emit-relocs -nostdlib

# The command line that starts QEMU: the MPS2-AN385 board, no monitor
# console, no window, UART0 connected to this terminal, our image as
# the program. There is no semihosting: all output goes through the UART.
QEMU := qemu-system-arm -machine mps2-an385 -monitor null -nographic -serial stdio -kernel $(BIN)
QEMU += -device loader,file=$(IMAGE),addr=$(IMAGE_ADDR),force-raw=on

# These targets are command names, not files on disk.
.PHONY: all clean image run debug run-trace test test-list format format-check programs

# Default goal: build the image.
all: $(BIN)

# Compile a program source or runtime file with the program flags
$(PROG_BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PROG_CFLAGS) -c $< -o $@

$(PROG_BUILD_DIR)/obj/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(PROG_CFLAGS) -c $< -o $@

# Link one program with its own runtime. -lgcc goes last so the linker
# sees which helpers the objects need before it searches the library
$(PROG_BUILD_DIR)/%.elf: $(PROG_BUILD_DIR)/obj/$(PROGRAM_DIR)/%.o $(PROG_RUNTIME_OBJS) scripts/programs.ld
	$(CC) $(PROG_CFLAGS) $(PROG_LDFLAGS) $(filter %.o, $^) -lgcc -o $@

# build every program in programs/ without packing them
programs: $(PROG_ELFS)


# Compile one .c file to a .o file, creating its build folder first.
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble one .s file (hand-written assembly) to a .o file. We still
# call $(CC): GCC hands a ".s" file straight to the assembler, and the
# same CPU flags make the assembly match what the C files use.
$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Link every object file into the final image. The linker script is a
# prerequisite too, so editing it makes make link again.
$(BIN): $(OBJECTS) scripts/mps2_m3.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $(BIN)

# Pack every program into the IMG1 image as NAME=build/programs/NAME.elf
# The file name is the IMG1 name, so it must follow the IMG1 rules.
$(IMAGE): $(PROG_ELFS) $(IMAGE_TOOL)
	@test -n "$(PROG_NAMES)" || { printf '%s\n' 'make image: no programs in $(PROGRAM_DIR)/'; exit 2; }
	python3 $(IMAGE_TOOL) image $@ $(foreach p, $(PROG_NAMES), $(p)=$(PROG_BUILD_DIR)/$(p).elf)

# Build the kernel and the image
image: $(BIN) $(IMAGE)

# Build, load the image at its fixed FLASH address, then run in QEMU.
run: $(BIN) $(IMAGE)
	$(QEMU)

# Same as `run`, but paused for a debugger:
#   -S             - freeze the emulated CPU at reset
#   -gdb tcp::1234 - open a GDB server on TCP port 1234 and wait there
# From a second terminal, connect with:
#   arm-none-eabi-gdb build/scratch-os.axf   (on Linux: gdb-multiarch)
# (for a demo build, e.g. DEMO=rogue: build/demo-rogue/scratch-os.axf;
#  for QUANTUM=2: build/quantum-2/scratch-os.axf)
# and inside gdb:
#   target remote :1234
debug: $(BIN) $(IMAGE)
	$(QEMU) -S -gdb tcp::1234

# Same as `run`, plus `-d unimp`: QEMU prints a message whenever the
# program touches a register or device that QEMU does not implement.
run-trace: $(BIN) $(IMAGE)
	$(QEMU) -d unimp

# The published kit is in test-kit/, but program/image builds and the
# shell/loader still need implementing before it can validate the kernel.
# CI will call this target for the big suite once runtime tests are enabled.
test:
	@printf '%s\n' 'make test: program/image builds and shell/loader integration are not implemented; see test-kit/README.md.'
	@exit 2

# Validate all big-suite manifests and expected transcripts without running QEMU.
# This project uses the big suite, which includes all ten common tests.
test-list:
	python3 test-kit/run_tests.py --suite big --list

# Remove every generated file, demo and quantum builds included.
clean:
	rm -rf build

# Use tracked C sources and headers so build outputs are never formatted.
# Preserve the professor-supplied test programs as published.
# NUL separators keep filenames containing spaces intact.
format:
	git ls-files -z -- '*.c' '*.h' ':!test-kit/**' | xargs -0 $(CLANG_FORMAT) --style=file -i

# --dry-run leaves files untouched; --Werror makes differences fail the check.
format-check:
	git ls-files -z -- '*.c' '*.h' ':!test-kit/**' | xargs -0 $(CLANG_FORMAT) --style=file --dry-run --Werror
