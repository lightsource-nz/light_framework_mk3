#ifndef _CLI_PRIVATE_H
#define _CLI_PRIVATE_H

extern void light_stream_setup();
extern uint8_t cli_task(struct light_application *app);

// root_command and the app-root resolution now live in light_core -- see light_command.h,
// which declares both
extern struct light_cli_invocation static_invoke;

#endif