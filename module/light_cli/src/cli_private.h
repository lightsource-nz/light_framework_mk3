#ifndef _CLI_PRIVATE_H
#define _CLI_PRIVATE_H

extern void light_cli_message_init();
extern uint8_t cli_task(struct light_application *app);

extern struct light_command root_command;
extern struct light_cli_invocation static_invoke;

#endif