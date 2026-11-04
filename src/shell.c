#include <stdint.h>
#include "kernel/task.h"
#include "lib/printf.h"
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

        // Ciclo interno: polling per costruire la stringa carattere per carattere
        while ( 1 )
        {
            /** 
             * la sys_read è una system call non bloccante: interroghiamo il kernel per sapere se 
             * l'hardware della UART ha ricevuto qualcosa
            */
            int c = sys_read();
            
            /* se non viene premuto nessun tasto il buffer del kernel è vuoto */
            if ( c == -1 )
            {
                /* per non sprecare cicli di clock, grazie a task_yield cediamo la CPU agli altri task */
                task_yield();
                continue;
            }
            
            if ( c == '\r' || c == '\n' )
            {
                printf( "\n" );
                buffer[ i ] = '\0';/* chiudiamo la stringa affinché C possa leggerla */
                break;
            }
            else
            {
                /**
                 *  implementiamo il silent drop: 
                 * accettiamo il carattere solo se abbiamo ancora spazio,
                 * altrimenti ignoriamo completamente l'input
                 */
                if ( i < 31 )
                {
                    printf( "%c", (char)c ); /* echo a schermo per l'utente */
                    buffer[ i ] = (char)c; /* salviamo nel nostro buffer */
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