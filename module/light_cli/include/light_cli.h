#ifndef _LIGHT_CLI_H
#define _LIGHT_CLI_H

#include <threads.h>
#include <stdarg.h>

#define LIGHT_CLI_MAX_REF_DEPTH                 8

#define to_command(ptr) container_of(ptr, struct light_command, header)

#define LIGHT_CLI_MAX_SUBCOMMANDS               16
#define LIGHT_CLI_MAX_OPTIONS                   16
#define LIGHT_CLI_MAX_ARGS                      16
#define LIGHT_CLI_OPTION_RAWVALUE_MAX           32
#define LIGHT_CLI_OPTION_VALUE_MAX              32
#define LIGHT_CLI_MAX_COMMAND_DEPTH             LIGHT_CLI_MAX_SUBCOMMANDS

#define LIGHT_CLI_OPTION                        0
#define LIGHT_CLI_SWITCH                        1
struct light_cli_option {
        struct light_command *command;
        uint8_t type;
        const char code;
        const uint8_t *name;
        const uint8_t *description;
};
struct light_cli_invocation;
struct light_command {
        struct light_command *parent;
        struct light_object header;
        const uint8_t *short_name;
        const uint8_t *full_name;
        const uint8_t *description;
        struct light_cli_invocation_result (*handler)(struct light_cli_invocation *);
        uint8_t arg_min;
        uint8_t arg_max;
        uint8_t option_count;
        uint8_t child_count;
        struct light_cli_option *option[LIGHT_CLI_MAX_OPTIONS];
        struct light_command *child[LIGHT_CLI_MAX_SUBCOMMANDS];
};
struct light_cli_option_value {
        const struct light_cli_option *option;
        const uint8_t *value;
};
struct light_cli_invocation {
        struct light_command *target;
        uint8_t option_count;
        struct light_cli_option_value option[LIGHT_CLI_MAX_OPTIONS];
        uint8_t args_bound;
        const uint8_t *arg[LIGHT_CLI_MAX_ARGS];
};
#define LIGHT_CLI_RESULT_SUCCESS        0
#define LIGHT_CLI_RESULT_ALIAS          1
#define LIGHT_CLI_RESULT_ERROR          2
struct light_cli_invocation_result {
        uint8_t code;
        union value {
                struct light_command *command;
        } value;
};
#define Result_Success (struct light_cli_invocation_result) {.code = LIGHT_CLI_RESULT_SUCCESS}
#define Result_Error (struct light_cli_invocation_result) {.code = LIGHT_CLI_RESULT_ERROR}
#define Result_Alias(target) \
        (struct light_cli_invocation_result) { \
                .code = LIGHT_CLI_RESULT_ALIAS, \
                .value.command = target \
        }

extern struct lobj_type ltype_cli_command;
// static command max-args value is determined at load time by the size of .arg_name
#define Light_Command_Static(_name, _parent, _desc, _handler, _arg_min, _arg_max, ...) \
        { \
                .header = Light_Object_RO("light_cmd:"_name, NULL, &ltype_cli_command), \
                .short_name = _name, \
                .parent = _parent, \
                .description = _desc, \
                .handler = _handler, \
                .arg_min = _arg_min, \
                .arg_max = _arg_max, \
                .option = { __VA_ARGS__ } \
        }

// FIXME the arg ordering on these macros is fucking wack
#define Light_Command_Option_Type(name, command, type, code, description) \
        { command, type, code, name, description }

#define Light_Command_Switch(command, code, name, description) \
        Light_Command_Option_Type(name, command, LIGHT_CLI_SWITCH, code, description)

#define Light_Command_Option(command, code, name, description) \
        Light_Command_Option_Type(name, command, LIGHT_CLI_OPTION, code, description)

#define Light_Command_Declare(sym_name, parent) \
        extern struct light_command sym_name

extern void light_cli__autoload_command(void *object);
extern void light_cli__autoload_option(void *object);

#define Light_Command_Define(sym_name, parent, name, description, handler, _arg_min, _arg_max, ...) \
        struct light_command __static_descriptor sym_name = \
                Light_Command_Static(name, parent, description, handler, _arg_min, _arg_max, __VA_ARGS__); \
        static const __static_object struct light_static_object autoload_## sym_name = \
                Light_Static_Object(&sym_name, light_cli__autoload_command);

#define Light_Command_Option_Declare(sym_name, command) \
        extern struct light_cli_option sym_name

#define Light_Command_Option_Type_Define(sym_name, command, type, name, code, description) \
        struct light_cli_option __static_descriptor sym_name = \
                        Light_Command_Option_Type(name, command, type, code, description); \
        const struct light_static_object __static_object autoload_## sym_name = \
                Light_Static_Object(&sym_name, light_cli__autoload_option)
#define Light_Command_Option_Define(sym_name, command, name, code, description) \
                Light_Command_Option_Type_Define(sym_name, command, LIGHT_CLI_OPTION, name, code, description)
#define Light_Command_Switch_Define(sym_name, command, name, code, description) \
                Light_Command_Option_Type_Define(sym_name, command, LIGHT_CLI_SWITCH, name, code, description)

extern struct light_command root_command;

// called at module load-time by framework
extern void light_cli_init();

static inline const uint8_t *light_cli_command_get_full_name(struct light_command *command)
{
        return command->full_name;
}
static inline const struct light_command *light_cli_command_get_parent(struct light_command *command)
{
        return command->parent;
}
static inline const uint8_t *light_cli_get_object_id(struct light_command *command)
{
        return light_object_get_name(&command->header);
}
static inline const uint8_t *light_cli_command_get_short_name(struct light_command *command)
{
        return command->short_name;
}
static inline const uint8_t *light_cli_command_get_description(struct light_command *command)
{
        return command->description;
}

static inline const char light_cli_option_get_code(struct light_cli_option *option)
{
        return option->code;
}
static inline const uint8_t *light_cli_option_get_name(struct light_cli_option *option)
{
        return option->name;
}
static inline uint8_t light_cli_option_get_type(struct light_cli_option *option)
{
        return option->type;
}
static inline const uint8_t *light_cli_option_get_description(struct light_cli_option *option)
{
        return option->description;
}

extern struct light_cli_option *light_cli_find_command_option(
                                struct light_command *command, const uint8_t *name);
static inline struct light_cli_option *light_cli_find_option(const uint8_t *name)
{
        return light_cli_find_command_option(NULL, name);
}

extern const uint8_t *light_cli_invocation_get_arg_value(struct light_cli_invocation *invoke, uint8_t index);
extern const uint8_t *light_cli_invocation_get_option_value(struct light_cli_invocation *invoke, const uint8_t *option_name);
static inline bool light_cli_invocation_option_is_set(struct light_cli_invocation *invoke, const uint8_t *option_name)
{
        return light_cli_invocation_get_option_value(invoke, option_name) != NULL;
}
static inline bool light_cli_invocation_get_switch_value(struct light_cli_invocation *invoke, const uint8_t *option_name)
{
        return light_cli_invocation_option_is_set(invoke, option_name);
}

// called at application load-time by framework
extern uint8_t light_cli_process_command_line(struct light_command *root, struct light_cli_invocation *invoke, int argc, char *argv[]);
extern uint8_t light_cli_dispatch_command_line(struct light_cli_invocation *invoke);

// command and option API
extern struct light_command *light_cli_create_command(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                struct light_cli_invocation_result (*handler)(struct light_cli_invocation *));
extern void light_cli_register_command(
                                struct light_command *parent,
                                struct light_command *command);

extern struct light_cli_option *light_cli_create_option_ctx(
                                struct light_command *parent,
                                const uint8_t code,
                                const uint8_t *name,
                                bool arg,
                                const uint8_t *description);

static inline struct light_cli_option *light_cli_create_option(
                                const uint8_t code,
                                const uint8_t *name,
                                bool arg,
                                const uint8_t *description)
{
        return light_cli_create_option_ctx(NULL, code, name, arg, description);
}
extern void light_cli_register_option_ctx(
                                struct light_command *parent,
                                struct light_cli_option *option);

static inline void light_cli_register_option(
                                struct light_cli_option *option)
{
        return light_cli_register_option_ctx(NULL, option);
};
static inline struct light_cli_option *light_cli_create_switch_ctx(
                                struct light_command *parent,
                                const uint8_t code,
                                const uint8_t *name,
                                const uint8_t *description)
{
        return light_cli_create_option_ctx(parent, code, name, false, description);
}
static inline struct light_cli_option *light_cli_create_switch(
                                const uint8_t code,
                                const uint8_t *name,
                                const uint8_t *description)
{
        return light_cli_create_switch_ctx(NULL, code, name, description);
}
extern struct light_command *light_cli_find_command(
                                struct light_command *parent, const uint8_t *name);

#endif