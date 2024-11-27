/*
 *  light_cli/src/command.c
 *  functions implementing the light_cli command API
 * 
 *  authored by Alex Fulton
 *  created november 2024
 * 
 */

#include <light_cli.h>
#include <stdio.h>

#include "cli_private.h"

#define MAX_TOKENS 64

struct command_token {
        uint8_t type;
};

static uint8_t next_token;
static struct command_token tokens[MAX_TOKENS];
static uint8_t next_root_command;
static struct light_command *root_command[LIGHT_CLI_MAX_SUBCOMMANDS];

void light_cli_init()
{
        light_cli_message_init();
        next_token = 0;
        next_root_command = 0;
}

void light_command_process_command_line(int argc, char *argv[])
{
        // command line parsing algorithm

        //   first pass classifies all tokens as either option flags or bare strings
        // (where bare strings may be either the name of a command, or an argument
        // [to either an option or command]).
        //   second pass, which relies on the prior construction of a table of all
        // options and commands and the arguments they take, performs the task of
        // identifying commands and options by name, and binding argument values to
        // the commands and options which expect them.
        for(int i = 0; i < argc; i++) {

        }
}

struct light_command *light_cli_register_subcommand(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                void (*handler)(struct light_command *))
{
        if(parent && parent->child_count >= LIGHT_CLI_MAX_SUBCOMMANDS) {
                light_warn("failed to register command '%s', parent command exceeded maximum subcommand count", name);
                return NULL;
        }
        struct light_command *command = light_alloc(sizeof(struct light_command));

        command->name = name;
        command->description = description;
        command->handler = handler;

        return command;
}
extern void light_cli_command_add_child_(
                                struct light_command *parent,
                                struct light_command *child
);
extern uint32_t light_cli_register_switch_ctx(
                                struct light_command *parent,
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                const uint8_t *description);
extern uint32_t light_cli_register_option_ctx(
                                struct light_command *parent,
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                uint8_t args_count,
                                const uint8_t *description);

struct light_command *light_cli_find_subcommand(
                                struct light_command *parent, uint8_t *sub_name);
bool light_cli_get_switch_value(uint32_t option_id);
uint8_t *light_cli_get_option_value(uint32_t option_id);
