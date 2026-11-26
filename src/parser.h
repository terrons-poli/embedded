#ifndef PARSER_H
#define PARSER_H

/* cmd contains cmd_length input bytes followed by a NUL terminator. */
void execute_command( const char *cmd, unsigned int cmd_length );

#endif