# Scratch-OS Project Test Kit

This is the published test kit for the [scratch-OS run-time program loader
assignment](../ASSIGNMENT.md). It contains the exact public suites used for
assessment. The runner uses only the Python standard library and starts a fresh
QEMU process for every test.

Run all commands below from the repository root, the directory containing
`Makefile` and `ASSIGNMENT.md`. Build outputs stay under `build/test-kit/`,
which Git ignores. The runner locates its suites and expected transcripts
relative to `run_tests.py`, so it also works from other directories when you
supply the correct kernel and image paths.

The current project base has no program build rules, image packer, shell or
loader yet. Listing the suites works now; running them requires those project
components, ARM bare-metal GCC and `qemu-system-arm`. Listing tests does not
validate the kernel. The repository's `make test` target is still unavailable.

## Pull-request checks

This project uses only the **big suite**, which includes the ten common tests
and four additional ELF/W^X tests. The small-suite files remain in the published
kit for reference.

The `Professor tests` workflow runs on pushes, pull requests and manual starts.
It runs `make test-list` to validate the big-suite manifest and all 14 expected
transcripts, then compiles the kernel with ARM bare-metal GCC. This catches
broken test-data paths and kernel build errors now. It does not check runtime
correctness yet. The workflow summary says when runtime tests are disabled,
and the `Big suite in QEMU` job is skipped.

The runtime job uses an explicit switch rather than guessing readiness from
the presence of source files. A file can exist while its implementation is
still incomplete. To activate runtime tests:

1. Implement the program build, image packer, shell and loader.
2. Replace the `make test` placeholder with a reproducible build of the kernel,
   all nine big-suite test programs, and `build/test-kit/big/fs.img`. Use the
   big fixture generator below to include the deliberately malformed inputs.
   Finish the target with
   `python3 test-kit/run_tests.py --suite big --kernel build/scratch-os.axf --image build/test-kit/big/fs.img`.
   Let a build error or nonzero runner exit fail the target.
3. Verify `make clean && make test` succeeds locally, then set the GitHub
   Actions repository variable `PROFESSOR_TESTS_ENABLED` to `true` under
   Settings > Secrets and variables > Actions > Variables. The workflow uses
   GitHub's [configuration variables](https://docs.github.com/en/actions/reference/workflows-and-actions/contexts#vars-context)
   to control the runtime job.

The workflow installs GCC and QEMU for the runtime job and calls `make test`
in a fresh checkout. For example, a UART mismatch will make the runner return
1, which fails that job. Enabling the switch before implementing `make test`
also fails the job; it never treats missing prerequisites as a passing test.
Once runtime tests are enabled, require `Big suite in QEMU` in your branch
rules to prevent merging a failing suite. Until then, the kernel build and
configuration check can be required separately.

Both jobs use a read-only GitHub token and the same pinned checkout action as
the formatting workflow. Pinning fixes the action to a reviewed version;
read-only access is enough because these checks do not publish changes. Each
job has a ten-minute limit so a stuck build or emulator cannot run indefinitely.

## Contents

- `run_tests.py`: QEMU runner and exact UART transcript comparator;
- `suites/`: the 10-test small manifest and 14-test big manifest;
- `small/programs/`: common test-program sources used by both versions;
- `big/programs/`: the two additional W^X test-program sources;
- `small/generate_fixtures.py`: deterministic SFT1 malformed-file and IMG1
  generator;
- `big/generate_fixtures.py`: deterministic malformed-ELF and IMG1 generator;
- `small/expected/` and `big/expected/`: exact expected UART transcripts.

No prebuilt program, image, or kernel is included. Build the sources with your
project's program startup, address-zero linker script, system-call stubs,
`printf` implementation, and ARM bare-metal compiler settings required by the
assignment. Keep the listed IMG1 names unchanged because the suites send those
names to the shell.

## Build The Small-Suite Image

Compile every source in `test-kit/small/programs/`. Convert the resulting ELF files with
your `tools/mkimage.py flat` command. The example below assumes the converted
files are under `build/test-kit/flats/`:

```sh
python3 test-kit/small/generate_fixtures.py \
  --output-dir build/test-kit/small/files \
  --image build/test-kit/small/fs.img \
  print_plain=build/test-kit/flats/print_plain.sft \
  global_state=build/test-kit/flats/global_state.sft \
  thumb_ptr=build/test-kit/flats/thumb_ptr.sft \
  nonzero_slot=build/test-kit/flats/nonzero_slot.sft \
  normal_return=build/test-kit/flats/normal_return.sft \
  explicit_exit=build/test-kit/flats/explicit_exit.sft \
  kernel_store=build/test-kit/flats/kernel_store.sft
```

The generator appends `bad_magic`, `bad_header`, `too_large`, and `bad_reloc`
and writes one combined IMG1 image. Its arguments are `NAME=FILE`, so adapt only
the file paths if your build uses another output layout.

## Build The Big-Suite Image

Compile every source in `test-kit/small/programs/` and `test-kit/big/programs/` as unchanged ELF
files satisfying the assignment ABI. The first argument must be `print_plain`;
the generator uses that valid ELF reproducibly as the basis for malformed ELF
fixtures.

```sh
python3 test-kit/big/generate_fixtures.py \
  --output-dir build/test-kit/big/files \
  --image build/test-kit/big/fs.img \
  print_plain=build/test-kit/programs/print_plain.elf \
  global_state=build/test-kit/programs/global_state.elf \
  thumb_ptr=build/test-kit/programs/thumb_ptr.elf \
  nonzero_slot=build/test-kit/programs/nonzero_slot.elf \
  normal_return=build/test-kit/programs/normal_return.elf \
  explicit_exit=build/test-kit/programs/explicit_exit.elf \
  kernel_store=build/test-kit/programs/kernel_store.elf \
  wx_write_code=build/test-kit/programs/wx_write_code.elf \
  wx_exec_data=build/test-kit/programs/wx_exec_data.elf
```

The generator preserves the supplied valid ELFs byte for byte, adds the four
common malformed cases plus `bad_rel_type` and `truncated_elf`, and packs them
into IMG1.

## List And Run Tests

Listing validates the selected suite
configuration and does not require QEMU or build artifacts:

```sh
python3 test-kit/run_tests.py --suite small --list
python3 test-kit/run_tests.py --suite big --list
```

For this project's selected suite, `make test-list` is the shortcut for the
second command. CI runs only that suite.

Run a complete suite with your kernel and the matching generated image:

```sh
python3 test-kit/run_tests.py --suite small \
  --kernel build/scratch-os.axf \
  --image build/test-kit/small/fs.img

python3 test-kit/run_tests.py --suite big \
  --kernel build/scratch-os.axf \
  --image build/test-kit/big/fs.img
```

Repeat `--test NAME` to select tests. The startup, per-test, and whole-run
limits default to 5, 5, and 90 seconds; change them with `--startup-timeout`,
`--test-timeout`, and `--global-timeout`. Use `--qemu COMMAND` if
`qemu-system-arm` is not on `PATH` under that name.

Configuration errors return 2, a test or QEMU failure returns 1, and a fully
passing suite returns 0. Each result is `PASS <name>` or
`FAIL <name>: <reason>`, followed by a deterministic summary. Comparison is
exact after converting host CRLF to LF; spaces, prompts, addresses, ids, and
all other bytes remain significant.
