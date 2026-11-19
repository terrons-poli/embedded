/*
 * Embedded Operating Systems and Architecture course
 * Licensed under the MIT License. See LICENSE in the repository root.
 *
 * Contact: Stefano Di Carlo <stefano.dicarlo@polito.it>
 *
 * MPU set-up: see kernel/mpu.h.
 *
 * Region 0 lets tasks read and run FLASH (code and constants).
 * Region 1 lets a task read and write its own partition.
 * Region 2 gives a loaded program's code read-only, executable access.
 * Any other access by a task raises a MemManage fault.
 */

#include <stdint.h>
#include "cmsdk.h"
#include "kernel/mpu.h"

void mpu_init( void )
{
  /* Region 0 = FLASH, from address 0, 4 MB = 2^22 bytes.
     * Read-only for everyone, and code may run from it (no XN). */
  MPU->RBAR = 0x00000000 | MPU_RBAR_VALID | 0;                                               /* = 0x00000010 */
  MPU->RASR = MPU_RASR_AP( MPU_AP_RO ) | MPU_RASR_C | MPU_RASR_SIZE( 22 ) | MPU_RASR_ENABLE; /* = 0x0602002B */

  /* Switch the MPU on. PRIVDEFENA: the kernel (privileged) keeps the
     * normal memory map, so only tasks are restricted. */
  MPU->CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA; /* = 5 */
  __asm volatile( "dsb" ::: "memory" );              /* finish the writes ... */
  __asm volatile( "isb" ::: "memory" );              /* ... before the next instruction */
}

void mpu_set_task_region( uint32_t base, uint32_t size, int executable,
                          uint32_t code_size )
{
  /* The SIZE field holds log2(size) - 1. __builtin_ctz counts the
     * trailing zero bits, which is log2 of a power of two.
     * Example: 4096 = 2^12, so SIZE = 11. */
  uint32_t log2_size = __builtin_ctz( size );

  /* Region 1 covers the whole partition. The current tasks all set
   * executable to zero, so only FLASH and the region 2 overlay can run. */
  MPU->RBAR = base | MPU_RBAR_VALID | 1;                                                                                                                  /* 4 KB at 0x20000000: 0x20000011 */
  MPU->RASR = ( executable ? 0u : MPU_RASR_XN ) | MPU_RASR_AP( MPU_AP_FULL_RW ) | MPU_RASR_S | MPU_RASR_C | MPU_RASR_SIZE( log2_size ) | MPU_RASR_ENABLE; /* 4 KB: 0x13060017 */

  /* Region 2 takes priority over region 1 for the leading subregions
   * occupied by code, rounded up to 512 bytes. Clear the previous
   * overlay when switching to a task whose code runs from FLASH. */
  if ( code_size == 0 || code_size > 3072 )
  {
    MPU->RNR = 2;
    MPU->RASR = 0;
  }
  else
  {
    uint32_t n_subregions = ( code_size + MPU_CODE_SUBREGION_SIZE - 1 ) / MPU_CODE_SUBREGION_SIZE;
    uint32_t active = ( 1u << n_subregions ) - 1;
    uint32_t srd = ~active & 0xFF;

    MPU->RBAR = base | MPU_RBAR_VALID | 2;
    MPU->RASR = MPU_RASR_AP( MPU_AP_RO ) | ( srd << 8 ) | MPU_RASR_S | MPU_RASR_C | MPU_RASR_SIZE( log2_size ) | MPU_RASR_ENABLE;
  }

  __asm volatile( "dsb" ::: "memory" );
  __asm volatile( "isb" ::: "memory" );
}
