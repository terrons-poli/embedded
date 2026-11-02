    .syntax unified
    .cpu cortex-m3
    .thumb


    .section .text._start, "ax", %progbits
    .global _start
    .type _start, %function
    .thumb_func
_start:
    bl  main
    bl  sys_exit
loop:                               /* safety loop */
    b loop
    .size _start, . - _start
