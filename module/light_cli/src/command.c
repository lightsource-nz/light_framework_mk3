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
#include <string.h>

#include "cli_private.h"

#define TYPE_NAME_COMMAND               "light_cli:command"

static void command_release(struct light_object *cmd);
struct lobj_type ltype_cli_command = {
        .id = TYPE_NAME_COMMAND,
        .release = command_release
};

#define MAX_TOKENS 64

struct command_token {
        uint8_t type;
};

static uint8_t next_token;
static struct command_token tokens[MAX_TOKENS];

//   we use this structure as a placeholder to store actual root-level commands,
// but it has no name and cannot be invoked
static struct light_command root_command;

static void command_release(struct light_object *cmd)
{
        light_free(to_command(cmd));
}
void light_cli_init()
{
        light_cli_message_init();
        next_token = 0;
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
struct light_command *light_cli_create_subcommand(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                void (*handler)(struct light_command *))
{
        struct light_command *command;
        if(!(command = light_alloc(sizeof(struct light_command)))) {
                light_warn("could not create new command '%s', failed to allocate memory", name);
                return NULL;
        }

        command->name = name;
        command->description = description;
        command->handler = handler;
        light_cli_register_subcommand(parent, command);
        return command;
}
void light_cli_register_subcommand(
                                struct light_command *parent,
                                struct light_command *command)
{        
        if(parent == NULL) return light_cli_register_subcommand(&root_command, command);
        if(parent && parent->child_count >= LIGHT_CLI_MAX_SUBCOMMANDS) {
                light_warn("failed to register command '%s', parent command exceeded maximum subcommand count", command->name);
        }
        parent->child[parent->child_count++] = command;
        light_trace("added subcommand '%s' to command '%s'", command->name);
}
void light_cli_register_option_ctx(
                                struct light_command *parent,
                                struct light_cli_option option)
{
        if(!parent->option_count >= LIGHT_CLI_MAX_OPTIONS) {
                parent->options[parent->child_count++] = option;
        } else {
                light_warn("could not add option '%s' to command '%s': max options reached", light_cli_option_get_name(&option), light_cli_command_get_name(parent));
        }
}

struct light_command *light_cli_find_subcommand(
                                struct light_command *parent, uint8_t *name)
{
        if(parent == NULL) {
                return light_cli_find_subcommand(&root_command, name);
        }
        for(uint8_t i = 0; i < parent->child_count && i < LIGHT_CLI_MAX_SUBCOMMANDS; i++) {
                if(strncmp(light_cli_command_get_name(parent->child[i]), name, LIGHT_OBJ_NAME_LENGTH)) {
                        return parent->child[i];
                }
        }
        return NULL;
}
bool light_cli_get_switch_value(uint32_t option_id);
uint8_t *light_cli_get_option_value(uint32_t option_id);
