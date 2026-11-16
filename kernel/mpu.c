/*
 * Embedded Operating Systems and Architecture course
 * Licensed under the MIT License. See LICENSE in the repository root.
 *
 * Contact: Stefano Di Carlo <stefano.dicarlo@polito.it>
 *
 * MPU set-up: see kernel/mpu.h.
 *
 * Region 0 lets tasks read and run FLASH (code and constants).
 * Region 1 lets a task read and write its own partition, nothing else.
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

void mpu_set_task_region( uint32_t base, uint32_t size, int executable )
{
  /* The SIZE field holds log2(size) - 1. __builtin_ctz counts the
     * trailing zero bits, which is log2 of a power of two.
     * Example: 4096 = 2^12, so SIZE = 11. */
  uint32_t log2_size = __builtin_ctz( size );

  /* Region 1 = the task's partition: read-write for everyone, but
     * never executable (XN). base must be a multiple of size. */
  MPU->RBAR = base | MPU_RBAR_VALID | 1;                                                                                                                  /* 4 KB at 0x20000000: 0x20000011 */
  MPU->RASR = ( executable ? 0u : MPU_RASR_XN ) | MPU_RASR_AP( MPU_AP_FULL_RW ) | MPU_RASR_S | MPU_RASR_C | MPU_RASR_SIZE( log2_size ) | MPU_RASR_ENABLE; /* 4 KB: 0x13060017 */
  __asm volatile( "dsb" ::: "memory" );
  __asm volatile( "isb" ::: "memory" );
}

/* We want for each partition to have .text RX and all the rest RW.
 * We already have mpu set ( mpu_set_task_region(...) ) with RW and XN (executable = 0) covering the whole slot.
 * An MPU region has a 2^n dimension and base aligned to its dimension.
 * Instead, of course, a program code can have any dimension.
 * So there is the need to find a way to let code and data "fall" in an address that the MPU can represent.
 * The idea is then to use an extra region RX (region 2) with 8 subregions of 512, and disable the ones where
 * no .text is stored. (We will have a padding of 508 bytes in the worst case scenario)
*/
void mpu_set_code_region( uint32_t base, uint32_t code_size )
{
  if ( code_size == 0 || code_size > 3072 )
  {
    MPU->RNR = 2;
    MPU->RASR = 0;

    __asm volatile( "dsb" ::: "memory" );
    __asm volatile( "isb" ::: "memory" );

    return;
  }

  uint32_t n_subregions = ( code_size + 511 ) / 512; /*  rounds n_subregions up               */
  uint32_t active = ( 1u << n_subregions ) - 1;      /*  es: n_subregions = 4; 10000 -> 1111    */
  uint32_t srd = ( ~active & 0xFF );

  uint32_t log2_size = __builtin_ctz( 4096 );

  MPU->RBAR = base | MPU_RBAR_VALID | 2;
  MPU->RASR = MPU_RASR_AP( MPU_AP_RO ) | ( srd << 8 ) | MPU_RASR_S | MPU_RASR_C | MPU_RASR_SIZE( log2_size ) | MPU_RASR_ENABLE;

  __asm volatile( "dsb" ::: "memory" );
  __asm volatile( "isb" ::: "memory" );
}
