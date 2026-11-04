#include "parser.h"
#include "string.h"
#include "lib/printf.h"

void execute_command(const char* cmd){

    // Command 'help'
    if(strcmp(cmd, "help") == 0){
        printf("help - show commands\n");
        printf("ls - list programs\n");
        printf("run <name> - run a program\n");
    }

    /* TODO: implement strcmp */
    else if(strcmp(cmd, "ls") == 0){

        //TODO (Issue #35)

    }else if(strncmp(cmd, "run" , 4) == 0){
        // the 4th element in cmd is the name of the 
        const char *program_name = cmd + 4;

        //An empty line, run alone prints the error
        if(program_name[0] == '\0') {
            printf("error: unknown command\n");
        } else {
            //TODO (Issue #35)
        }
    }
    else{
        printf("error: unknown command\n");
    }
}