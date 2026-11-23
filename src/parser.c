#include "parser.h"
#include "lib/printf.h"
#include "lib/syscalls.h"

/* Compare at most n bytes, stopping at the first NUL. */
static int my_strncmp( const char *s1, const char *s2, unsigned int n )
{
  while ( n > 0 && *s1 && ( *s1 == *s2 ) )
  {
    s1++;
    s2++;
    n--;
  }
  if ( n == 0 )
    return 0;
  return *(const unsigned char *) s1 - *(const unsigned char *) s2;
}

void execute_command( const char *cmd, unsigned int cmd_length )
{

  /* Command 'help': Show available commands */
  if ( cmd_length == 4 && my_strncmp( cmd, "help", cmd_length ) == 0 )
  {
    printf( "help - show commands\n" );
    printf( "ls - list programs\n" );
    printf( "run <name> - run a program\n" );
  }

  /* Command 'ls': List all available programs */
  else if ( cmd_length == 2 && my_strncmp( cmd, "ls", cmd_length ) == 0 )
  {
    /* Get the total number of executable images from the kernel (SVC 11) */
    int count = sys_image_count();

    /* SVC 12 permits at most 16 bytes: 15 name characters and the final NUL. */
    char name_buffer[16];

    if ( count < 0 )
    {
      printf( "error: no valid image\n" );
    }
    else
    {
      for ( int i = 0; i < count; i++ )
      {
        /* Ask the kernel (SVC 12) to copy the name of the program at index 'i' 
                into our buffer. We pass the buffer size to prevent memory overflows. */
        int result = sys_image_name( i, name_buffer, sizeof( name_buffer ) );

        if ( result == 0 )
          printf( "%s\n", name_buffer );
      }
    }
  }
  /* Command 'run <name>': Execute a specific program */
  else if ( cmd_length > 4 && my_strncmp( cmd, "run ", 4 ) == 0 )
  {
    unsigned int i;
    for ( i = 0; i < cmd_length; i++ )
      if ( cmd[i] == '\0' )
        break;

    if ( i != cmd_length )
    {
      printf( "error: unknown command\n" );
    }
    else
    {
      /* The program name starts immediately after "run " (index 4) */
      const char *program_name = cmd + 4;

      /* Ask the kernel (SVC 13) to load and start the requested program */
      int task_id = sys_program_run( program_name );

      /* Handle specific kernel error codes */
      if ( task_id == -2 )
      {
        printf( "error: program not found\n" );
      }
      else if ( task_id == -3 )
      {
        printf( "error: no free slot\n" );
      }
      else if ( task_id < 0 )
      {
        /* Catch-all for any other negative value */
        printf( "error: program cannot be loaded\n" );
      }
      else
      {
        /* Ask the kernel (SVC 14) to block the shell until the task ends */
        sys_program_wait( task_id );
      }
    }
  }
  /* Unknown or malformed command */
  else
  {
    printf( "error: unknown command\n" );
  }
}
