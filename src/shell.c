#include <stdint.h>
#include "kernel/task.h"
#include "kernel/kprintf.h"
#include "lib/syscalls.h"

extern uint32_t _shell_part_start[], _shell_stack_top[]; /* shell_stack */
extern uint32_t _shell_data_start[], _shell_data_end[], _shell_data_load[]; /* shell_data */
extern uint32_t _shell_part_end[]; /* shell_pad */

void shell_main()
{
    while ( 1 )
  {
    char buffer[ 32 ];
    int i = 0;

    printf( "scratch> " );

    while ( 1 )
    {
      int c = sys_read();
      
      if ( c == -1 )
      {
        task_yield();
        continue;
      }

      if ( c == '\r' || c == '\n' )
      {
        printf( "\n" );
        buffer[ i ] = '\0';
        break;
      }
      else
      {
        if ( i < 31 )
        {
          printf( "%c", (char)c );
          buffer[ i ] = (char)c;
          i++;
        }
      }
    }
  }
}

void task_project_configure( void )
{
    task_configure_shell( "shell", shell_main,
        _shell_stack_top, (uint32_t)_shell_part_start,
        (uint32_t)_shell_part_end, _shell_data_start,
        _shell_data_end, _shell_data_load
    );
}