# Scratch-OS Project Base

This directory is the starting point for the program-loader project. It builds
and boots on QEMU before you add project code, but it is not a loader solution:
the shell, image reader, program loader, per-slot MPU policy, program build
files, and image-building tool are deliberately absent.

## Starting point

The base was derived from `../week-05/solution/`. It retains the Cortex-M3
startup and exception code, UART and SysTick drivers, MPU support, preemptive
round-robin scheduler, fault and scheduler tracing support, system-call
mechanism, and the small `printf` library. Tasks still run unprivileged on the
process stack (PSP), and the kernel still runs privileged on the main stack.

The week-05 teaching demos were removed. In particular, `task_a.c`,
`task_b.c`, their static task partitions, and the `DEMO=...` Makefile modes are
not part of this base. The task table contains the idle task plus inactive shell
and program entries; no entry contains loader code. Consequently, an untouched base prints the
kernel and scheduler banners and then remains in the idle task; it has no shell
prompt and cannot list or run programs.

## Memory map added for the project

The complete linker layout is in `scripts/mps2_m3.ld`. The relevant fixed
regions are:

| Address range | Size | Purpose |
|---|---:|---|
| `0x00000000` onward | within 4 MiB FLASH | Statically linked kernel image |
| `0x00200000` onward | image-dependent | Read-only `build/fs.img`, loaded by QEMU |
| `0x20000000`-`0x20000fff` | 4 KiB | Reserved static shell-task partition |
| `0x20001000`-`0x20001fff` | 4 KiB | Static idle-task partition |
| `0x20002000`-`0x20002fff` | 4 KiB | Program slot 0 |
| `0x20003000`-`0x20003fff` | 4 KiB | Program slot 1 |
| `0x20004000`-`0x20004fff` | 4 KiB | Program slot 2 |
| `0x20005000`-`0x20005fff` | 4 KiB | Program slot 3 |

The four program slots form one statically reserved pool from `0x20002000` to
`0x20005fff`. Each slot is 4 KiB and starts on a 4 KiB boundary, as required
for one MPU region. Loaded code, data, and stack must share that slot. The
linker reserves and exports the future shell partition even though the base has
no shell implementation. It also exports the pool, per-slot, size, count, and
image-address symbols; a successful build records their resolved values in
`build/output.map`.

The image address `0x00200000` is also set as `IMAGE_ADDR` in `Makefile`.
QEMU places the image there with its generic loader; the base contains no
FLASH-storage driver and does not currently inspect the image.

## Build targets

Run these commands from this directory. They require the ARM bare-metal GCC
toolchain and QEMU's ARM system emulator.

- `make` builds the kernel as `build/scratch-os.axf` and writes
  `build/output.map`. It does not build the file-system image.
- `make run` builds the kernel and ensures that `build/fs.img` exists, then
  starts QEMU `mps2-an385`. QEMU loads `build/fs.img` at `0x00200000` with
  `-device loader`. Stop QEMU with `Ctrl-C`.
- If no image exists, the file target for `build/fs.img` writes a small raw
  placeholder containing `SCRATCH-OS-PROJECT-IMAGE`. This only lets the
  untouched base exercise the QEMU image-loading path. It is not the specified
  project image format and contains no programs.
- `make image` first builds the kernel, then expects your student-created
  `tools/mkimage.py`. It runs
  `python3 tools/mkimage.py --output build/fs.img programs`. Until you create
  that tool, the target exits with status 2 and prints
  `make image: implement tools/mkimage.py first`. The `programs/` directory is
  present as the input location, but no program build pipeline is supplied.
- `make test` is intentionally unavailable at this stage. It prints that the
  common project test kit is not implemented and exits with status 2. The
  target will be wired to the published kit later; do not treat its current
  failure as a test result.
- `make clean` removes the generated `build/` directory, including the kernel,
  map, and image. A later `make run` therefore creates the placeholder image
  again unless you have already generated a real image.

`make run` does not call `make image`: it uses the existing `build/fs.img`, or
creates the placeholder when that file is absent. After implementing the host
tool, use `make image` to replace the placeholder before running the loader.

## System-call contract stubs

`lib/syscalls.h` reserves numbers and declares user-facing functions for the
four loader operations:

| Number | Stub | Intended operation |
|---:|---|---|
| 11 | `sys_image_count()` | Get the number of image directory entries |
| 12 | `sys_image_name()` | Copy one entry's name |
| 13 | `sys_program_run()` | Load and start a named program |
| 14 | `sys_program_wait()` | Wait for a loaded task to finish |

The matching `svc` stubs exist in `lib/syscalls.s`. The supplied wait handler is
wired to the task scaffold, but there is no program that it can wait for in the
untouched base. Image count/name and program run remain unimplemented and
return `-1`. Nothing in these stubs finds, validates, copies or starts a program.

## Given run-time scaffold

`kernel/task.c`, `kernel/task.h`, `kernel/faults.c` and
`kernel/fault_entry.s` provide the project-neutral machinery that teams use
rather than rewrite: four inactive program TCBs, generation-tagged ids,
waiter/completion bookkeeping, initial exception frames, return/exit/fault
completion, and safe loaded-task fault classification. The weak
`task_project_configure()` hook leaves the shell absent until student shell code
configures it with `task_configure_shell()`.

After validating, copying, zeroing and relocating a file, loader code calls
`task_program_create(slot, name, entry, stack_top, executable)` to attach the validated
entry to the supplied lifecycle. Loader code still owns slot selection,
failed-load non-reservation, entry/stack integration, and the MPU policy used
when that TCB is scheduled. The scaffold reclaims a successfully created task
on return, `exit`, or a recoverable fault and wakes matching waiters.

## What your team builds

Your team extends this base into a system that can find and validate programs in
a read-only image, copy and relocate a named program into a free slot, install
its validated entry through the supplied task hook, and set the slot's MPU
permissions. You also add the static shell through the supplied configuration
hook and the kernel handlers behind image count/name and program run. Correct
integration leaves failed loads free and lets the supplied lifecycle reclaim a
started slot when it returns, exits, or faults.

On the host side, your team supplies `tools/mkimage.py` to create
`build/fs.img` from program inputs in `programs/`. You also supply the missing
start-up and linker support needed to build loadable programs separately from
the kernel. Follow the assignment for the exact image layout, shell behaviour,
error handling, and the requirements of your team's project version; this
base does not implement or replace that contract.

## Code formatting

The `Formatting` GitHub Actions workflow checks all tracked `.c` and `.h` files
on every push and when a pull request is opened, updated, or reopened. It can
also be started manually. A formatting difference fails the `clang-format`
check. Assembly, linker scripts, and untracked build outputs are outside this
check.

The workflow uses clang-format 18, which is already available on GitHub's
Ubuntu 24.04 runner. It does not install packages or build the kernel. Local
formatting requires clang-format 18 too; the ARM compiler and QEMU are not
needed. Using the same formatter version avoids differences between local
results and CI.

Run these commands from the repository root:

```sh
make format-check  # Report formatting differences without editing files.
make format        # Apply the rules to all tracked C sources and headers.
```

If your clang-format 18 executable has another name or location, set it
explicitly, for example `make CLANG_FORMAT=/path/to/clang-format format-check`.
New files enter the check after `git add` because the file list comes from Git.
The same commands run locally and in CI so they check the same files and rules.

Edit `.clang-format` to choose the rules. It starts with LLVM defaults and
overrides them to use two spaces and no line-length limit. It keeps the project's
function and control-statement braces on new lines, spaces inside nonempty
parentheses except casts, and pointers written as `uint32_t *ptr`. Include order
and comment text are preserved. `ColumnLimit: 0` disables the limit explicitly;
removing that setting would restore LLVM's default of 80 columns.

`RemoveBracesLLVM: true` removes optional braces from simple `if`, `else`, `for`,
and `while` bodies. The body stays on its own line. Multiple statements still
need braces, and clang-format keeps them in some nested cases to avoid ambiguous
control flow. For example:

```c
if ( ready )
  uart_write( buf, len );

while ( pending )
{
  process_next();
  pending = has_more();
}
```

Review changes from `make format`, especially brace removal. When you add a
second statement to an unbraced body, add braces around both statements;
indentation alone does not make a statement part of an `if` or loop.

Checking every tracked C file makes a rule change apply to the whole project;
checking only changed lines would leave older code using different rules.
Existing code may need a one-time `make format` before the first check passes.
Review and commit the resulting formatting changes along with the rules.

To make a failed check prevent merging, configure a GitHub branch ruleset or
branch protection rule that requires the `clang-format` status check.
