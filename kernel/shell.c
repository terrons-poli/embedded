#include <stdint.h>
#include "kernel/task.h"
#include "kernel/mpu.h"
#include "lib/syscalls.h"

extern uint32_t _shell_part_start[], _shell_stack_top[]; /* shell_stack */
extern uint32_t _shell_data_start[], _shell_data_end[], _shell_data_load[]; /* shell_data */
extern uint32_t _shell_part_end[]; /* shell_pad */

void shell_main()
{
    while(1){
        task_yield();
    }
}

void task_project_configure( void )
{
    /**
     * facciamo il cast esplicito per evitare che il compilatore GCC dia 
     * warning della conversione implicita da puntatori a interi
     */
    task_configure_shell( "shell", shell_main,
        _shell_stack_top, (uint32_t)_shell_part_start,
        (uint32_t)_shell_part_end, _shell_data_start,
        _shell_data_end, _shell_data_load
    );
}