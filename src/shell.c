#include <stdint.h>
#include "kernel/task.h"
#include "lib/printf.h"
#include "lib/syscalls.h"
#include "parser.h"

extern uint32_t _shell_part_start[], _shell_stack_top[];
extern uint32_t _shell_data_start[], _shell_data_end[], _shell_data_load[];
extern uint32_t _shell_part_end[];

void shell_main()
{
  for ( ;; )
  {
    char buffer[32];
    unsigned int length = 0;

    printf( "scratch> " );

    /* Inner loop: polling to build the input string character by character */
    for ( ;; )
    {
      int c = sys_read();

      /* If no key is pressed, the kernel buffer is empty (returns -1) */
      if ( c == -1 )
      {
        task_yield();
        continue;
      }

      if ( c == '\r' || c == '\n' )
      {
        printf( "\n" );
        buffer[length] = '\0';
        break;
      }
      else if ( length < sizeof( buffer ) - 1 )
      {
        buffer[length] = (char) c;
        length++;
        printf( "%c", (char) c );
      }
    }
    execute_command( buffer, length );
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
