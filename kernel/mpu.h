/*
 * Embedded Operating Systems and Architecture course
 * Licensed under the MIT License. See LICENSE in the repository root.
 *
 * Contact: Stefano Di Carlo <stefano.dicarlo@polito.it>
 *
 * The MPU (Memory Protection Unit): the hardware that decides which
 * addresses unprivileged code may touch. The kernel uses three regions:
 *   region 0: all of FLASH, read-only for tasks (code and constants);
 *   region 1: the current task's partition, read-write;
 *   region 2: the loaded program's code in that partition, read-only and
 *             executable.
 * Everything else is off limits to tasks. The kernel can also access the
 * default memory map outside the enabled regions.
 */
#ifndef MPU_H
#define MPU_H

#include <stdint.h>

/* Region 2 splits a 4 KiB program slot into eight subregions of this size.
 * Code is protected in whole subregions, so the first writable byte of a
 * program slot is code_size rounded up to it (see ALIGN(512) in
 * scripts/programs.ld). */
#define MPU_CODE_SUBREGION_SIZE 512u

/* Set up region 0 (FLASH) and switch the MPU on. Called once by main(). */
void mpu_init( void );

/* Set the MPU permissions for the incoming task's partition.
 * size must be a power of two (at least 32), and base a multiple of size.
 * executable controls instruction fetches in the partition outside the
 * code overlay.
 * For a loaded program, pass executable = 0 and the byte size of the code
 * at base (up to 3072 bytes in a 4 KiB slot). Pass code_size = 0 when
 * the task runs from FLASH, to disable the code overlay. */
void mpu_set_task_region( uint32_t base, uint32_t size, int executable,
                          uint32_t code_size );

#endif /* MPU_H */
