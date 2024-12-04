#ifndef _LIGHT_CLI_H
#define _LIGHT_CLI_H

#include <light.h>

// only the 'fast' and 'faster' message types are handled by this facility
#define LIGHT_CLI_MSG_FAST                    0
#define LIGHT_CLI_MSG_FASTER                  1

// TODO the mqueue interface can probably be excluded from the public API
#define LIGHT_CLI_MQUEUE_DEPTH             32

struct light_cli_message {
        uint8_t flags;
        uint8_t *text;
        uint8_t argc;
        void *argv;
};

// .message[] is treated as a circular buffer, collisions are avoided by
// checking that .count <= LIGHT_CLI_MQUEUE_DEPTH before writes
struct light_cli_mqueue {
        struct light_object obj_header;
        uint8_t count;
        uint8_t head;
        struct light_cli_message message[LIGHT_CLI_MQUEUE_DEPTH];
};

extern struct lobj_type ltype_cli_message_queue;

#define to_mqueue(ptr) container_of(ptr, struct light_cli_mqueue, obj_header)
#define to_command(ptr) container_of(ptr, struct light_command, header)

#define Light_CLI_MQueue(_name) \
{ \
        .obj_header = Light_Object_RO(_name, NULL, &ltype_cli_message_queue), \
}
#define Light_CLI_MQueue_Static(_name) \
{ \
        .obj_header = Light_Object_Static_RO(_name, NULL, &ltype_cli_message_queue) \
}

#define Light_CLI_MQueue_Declare(name) \
        extern struct light_cli_mqueue *name

#define Light_CLI_MQueue_Define(name) \
        struct light_cli_mqueue __static_buffer _## name = Light_CLI_MQueue_Static(#name); \
        struct light_cli_mqueue __static_object *name = &_## name

#define LIGHT_CLI_MAX_SUBCOMMANDS               16
#define LIGHT_CLI_MAX_OPTIONS                   16

struct light_cli_option {
        char code;
        const uint8_t *name;
        uint8_t args_count;
        const uint8_t *description;
};
struct light_command {
        struct light_command *parent;
        struct light_object header;
        const uint8_t *name;
        const uint8_t *description;
        void (*handler)(struct light_command *);
        uint8_t option_count;
        uint8_t child_count;
        struct light_cli_option options[LIGHT_CLI_MAX_OPTIONS];
        struct light_command *child[LIGHT_CLI_MAX_SUBCOMMANDS];
};

extern struct lobj_type ltype_cli_command;

#define Light_Command_Static(name, parent, desc, handler, ...) \
        (struct light_command) { \
                .header = Light_Object_Static_RO(name, &parent->header, &ltype_cli_command), \
                .name = name, \
                .parent = parent, \
                .description = desc, \
                .handler = handler, \
                .options = { __VA_ARGS__ } \
        }

#define Light_Command_Option(command, code, name, args_count, description) \
        (struct light_cli_option) { code, name, (args_count), description }

#define Light_Command_Switch(code, name, description) \
        Light_Command_Option(code, name, 0, description)

#define Light_Command_Declare(sym_name) \
        extern const struct light_command *sym_name

#define Light_Command_Define(sym_name, name, parent, description, handler) \
        static const struct light_command __static_descriptor _## sym_name = Light_Command_Static(name, parent, description, handler); \
        const struct light_module __static_object *sym_name = &_## sym_name

#define Light_Command_Option_Declare(command, sym_name) \
        extern const struct light_cli_option *sym_name

#define Light_Command_Option_Define(command, sym_name, code, args_count, description, handler) \
        static const struct light_cli_option __static_descriptor _## sym_name = \
                        Light_Command_Option(name, parent, description, handler); \
        const struct light_cli_option __static_object *sym_name = &_## sym_name

Light_CLI_MQueue_Declare(light_cli_mqueue_default);

// called at module load-time by framework
extern void light_cli_init();

static inline struct light_command *light_cli_command_get_parent(struct light_command *command)
{
        return command->parent;
}
static inline const uint8_t *light_cli_command_get_name(struct light_command *command)
{
        return light_object_get_name(&command->header);
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
static inline uint8_t light_cli_option_get_args_count(struct light_cli_option *option)
{
        return option->args_count;
}
static inline const uint8_t *light_cli_option_get_description(struct light_cli_option *option)
{
        return option->description;
}

extern void light_cli_mqueue_init(struct light_cli_mqueue *queue);
extern void light_cli_mqueue_add(struct light_cli_mqueue *queue, uint8_t flags, uint8_t *text, uint8_t argc, void *argv);

//   synchronous command-line messages are written to stdout while the caller waits,
// so they should not be sent from signal or interrupt context
extern void light_cli_message_sync(const uint8_t *message);
extern void light_cli_message_f_sync(const uint8_t *format, ...);
//   'fast' CLI messages are put into a queue that is processed on the main stack,
// but still perform string formatting synchronously before queueing the message
extern void light_cli_message_fast(const uint8_t *message);
extern void light_cli_message_f_fast(const uint8_t *format, ...);
//   'faster' CLI messages defer all CPU-intensive work for asynchronous processing,
// with the consequence that all referenced objects must remain in scope until
// the kernel worker can perform string formatting
extern void light_cli_message_f_faster(const uint8_t *format, ...);
// called at application load-time by framework
extern void light_cli_process_command_line(int argc, char *argv[]);

// command and option API
extern struct light_command *light_cli_create_subcommand(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                void (*handler)(struct light_command *)); // TODO define type for parsed options and args
static inline struct light_command *light_cli_create_command(
                                const uint8_t *name,
                                const uint8_t *description,
                                void (*handler)(struct light_command *))
{
        return light_cli_create_subcommand(NULL, name, description, handler);
}
extern void light_cli_register_subcommand(
                                struct light_command *parent,
                                struct light_command *command); // TODO define type for parsed options and args
static inline void light_cli_register_command(struct light_command *command)
{
        light_cli_register_subcommand(NULL, command);
}

extern void light_cli_create_option_ctx(
                                struct light_command *parent,
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                uint8_t num_args,
                                const uint8_t *description);

static inline void light_cli_create_option(
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                uint8_t num_args,
                                const uint8_t *description)
{
        return light_cli_create_option_ctx(NULL, short_name, long_name, num_args, description);
}
extern void light_cli_register_option_ctx(
                                struct light_command *parent,
                                struct light_cli_option option);

static inline void light_cli_register_option(
                                struct light_cli_option option)
{
        return light_cli_register_option_ctx(NULL, option);
};
static inline void light_cli_create_switch_ctx(
                                struct light_command *parent,
                                const uint8_t *short_name,
                                const uint8_t *long_name,
                                const uint8_t *description)
{
        light_cli_create_option_ctx(parent, short_name, long_name, 0, description);
}

extern struct light_command *light_cli_find_subcommand(
                                struct light_command *parent, uint8_t *name);
static inline struct light_command *light_cli_find_command(uint8_t *name)
{
        return light_cli_find_subcommand(NULL, name);
}
bool light_cli_get_switch_value(uint32_t option_id);
uint8_t *light_cli_get_option_value(uint32_t option_id);

#endif