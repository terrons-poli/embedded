# Shared headers

`elf.h` is vendored unchanged from `/usr/arm-none-eabi/include/elf.h` on the
development machine using ARM GCC 15.2.0. Its header identifies it as derived
from musl's `include/elf.h`; the copyright and permission notice are preserved.
It depends only on the standard `<stdint.h>` header.

Keeping the complete header here makes ELF definitions available even when
the bare-metal toolchain does not ship its own `elf.h`. The kernel build's
`-Iinclude` selects this copy for `<elf.h>`. For example, `Elf32_Rel` and
`R_ARM_ABS32` now come from this repository on both developer machines and CI.

Keep this third-party file unchanged when updating it, and record the new
source here. Local formatting changes would make comparison with its source
harder.
