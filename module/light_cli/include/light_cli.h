#ifndef _LIGHT_CLI_H
#define _LIGHT_CLI_H

#include <light.h>

Light_Module_Declare(light_cli);

#define LIGHT_CLI_MAX_SUBCOMMANDS               16

#define LIGHT_CLI_CONTEXT_ROOT                  NULL

struct light_command {
        struct light_command *parent;
        const uint8_t *name;
        const uint8_t *description;
        void (*handler)(struct light_command *);
        uint8_t child_count;
        struct light_command *child[LIGHT_CLI_MAX_SUBCOMMANDS];
};
// called at module load-time by framework
extern void light_cli_init();
// called at application load-time by framework
extern void light_cli_process_command_line(int argc, char *argv[]);

// command and option API
extern struct light_command *light_cli_register_subcommand(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                void (*handler)(struct light_command *)); // TODO define type for parsed options and args
static inline struct light_command *light_cli_register_command(
                                const uint8_t *name,
                                const uint8_t *description,
                                void (*handler)(struct light_command *))
{
        return light_cli_register_subcommand(LIGHT_CLI_CONTEXT_ROOT, name, description, handler);
};
extern void light_cli_command_add_child_(
                                struct light_command *parent,
                                struct light_command *child
);
extern uint32_t light_cli_register_switch_ctx(
                                struct light_command *parent,
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                const uint8_t *description);
static inline uint32_t light_cli_register_switch(
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                const uint8_t *description)
{
        return light_cli_register_switch_ctx(LIGHT_CLI_CONTEXT_ROOT, short_name, long_name, description);
};
extern uint32_t light_cli_register_option_ctx(
                                struct light_command *parent,
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                uint8_t args_count,
                                const uint8_t *description);
static inline uint32_t light_cli_register_option(
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                uint8_t args_count,
                                const uint8_t *description)
{
        return light_cli_register_option_ctx(LIGHT_CLI_CONTEXT_ROOT, short_name, long_name, args_count, description);
};

struct light_command *light_cli_find_subcommand(
                                struct light_command *parent, uint8_t *sub_name);
bool light_cli_get_switch_value(uint32_t option_id);
uint8_t *light_cli_get_option_value(uint32_t option_id);

#endif