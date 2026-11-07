#include <stdint.h>
#include "kernel/task.h"
#include "lib/printf.h"
#include "lib/syscalls.h"
#include "parser.h"

extern uint32_t _shell_part_start[], _shell_stack_top[];                    /* shell_stack */
extern uint32_t _shell_data_start[], _shell_data_end[], _shell_data_load[]; /* shell_data */
extern uint32_t _shell_part_end[];                                          /* shell_pad */

void shell_main()
{
  while ( 1 )
  {
    char buffer[32];
    int i = 0;

    printf( "scratch> " );

    /* Inner loop: polling to build the input string character by character */
    while ( 1 )
    {
      /* 
        * sys_read is a non-blocking system call: we ask the kernel 
        * if the UART hardware has received any characters.
        */
      int c = sys_read();

      /* If no key is pressed, the kernel buffer is empty (returns -1) */
      if ( c == -1 )
      {
        /* 
        * To avoid wasting CPU cycles in a busy-wait loop, we use 
        * task_yield() to willingly give the CPU to other tasks.
        */
        task_yield();
        continue;
      }

      /* Check if the user pressed the Enter key (Carriage Return or Line Feed) */
      if ( c == '\r' || c == '\n' )
      {
        printf( "\n" );

        /* Null-terminate the string so standard C functions can safely read it */
        buffer[i] = '\0';
        break;
      }
      /* Handle Backspace (ASCII 8 '\b' or ASCII 127 DEL) */
      else if ( c == '\b' || c == 127 )
      {
        if ( i > 0 )
        {
          i--; /* Move back in the buffer */
          /* Erase character from terminal: backspace, overwrite with space, backspace */
          printf( "\b \b" );
        }
      }
      else
      {
        /*
        * Implement "silent drop" security feature: 
        * we accept the character only if we still have space in the buffer.
        * Otherwise, we completely ignore the input to prevent buffer overflows.
        */
        if ( i < 31 )
        {
          printf( "%c", (char) c ); /* Echo the character to the screen */
          buffer[i] = (char) c;     /* Store the character in our buffer */
          i++;
        }
      }
    }
    execute_command( buffer );
  }
}

void task_project_configure( void )
{
  /* 
     * Configure the shell task memory protection boundaries 
     * using the physical addresses provided by the linker script.     */
  task_configure_shell( "shell", shell_main,
                        _shell_stack_top, (uint32_t) _shell_part_start,
                        (uint32_t) _shell_part_end, _shell_data_start,
                        _shell_data_end, _shell_data_load );
}