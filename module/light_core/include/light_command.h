#ifndef _LIGHT_COMMAND_H
#define _LIGHT_COMMAND_H

/*
 *  light_core/light_command.h
 *  the command TREE: its types, its static-declaration macros and its registration
 *
 *  MOVED HERE FROM light_cli.h, because Light_Application_Define() declares an application's
 *  root command and therefore needs Light_Command_Define() to already exist. light.h could not
 *  reach into light_cli for that -- light_cli depends on light_core, not the reverse -- and the
 *  macro hook that briefly bridged the gap was a workaround for the layering being wrong rather
 *  than a design.
 *
 *  So the half of light_cli that is really a data structure lives here, in the module that owns
 *  every other framework-wide registry, and light_cli keeps the half that is really a program:
 *  the tokenizer, the parser, dispatch, and the line queue.
 *
 *  NAMING. The public API is light_command_* / light_command_option_*, matching light_object_*,
 *  light_module_* and light_stream_* -- the shape light_core already uses for every registry it
 *  owns. The plumbing an application never calls itself is light_core_*: the autoload hooks
 *  Light_Command_Define() emits, and the app-root resolution the framework drives.
 *
 *  WHAT KEPT THE light_cli_ PREFIX, and why it is not an oversight: struct light_cli_invocation
 *  and its result type. An invocation is a PARSED COMMAND LINE -- it holds bound arguments and
 *  option values, which only the parser produces and only a handler consumes. They appear here
 *  solely because struct light_command's handler pointer is typed in terms of them, which is a
 *  reference from the tree to the CLI rather than a sign that they belong to the tree.
 *
 *  INCLUDED FROM light.h, at the point where light_object and the static-object machinery it
 *  builds on are defined and before Light_Application_Define() uses it. It is not self-contained
 *  by design -- including it directly, first, would see none of that.
 */
#ifndef _LIGHT_CORE_H
#error "include <light.h> rather than <light_command.h> -- see the note above"
#endif

#define to_command(ptr) container_of(ptr, struct light_command, header)

// the shape of the tree
#define LIGHT_COMMAND_MAX_SUBCOMMANDS           16
#define LIGHT_COMMAND_MAX_OPTIONS               16
#define LIGHT_COMMAND_MAX_DEPTH                 LIGHT_COMMAND_MAX_SUBCOMMANDS

//   the parser's own limits, which live here only because struct light_cli_invocation does --
// see the naming note above. LIGHT_CLI_MAX_TOKENS is public because light_cli_tokenize_line()
// fills such an array on the caller's stack, and a caller sizing it independently would hand
// the parser more tokens than it reads
#define LIGHT_CLI_MAX_ARGS                      16
#define LIGHT_CLI_OPTION_RAWVALUE_MAX           32
#define LIGHT_CLI_OPTION_VALUE_MAX              32
#define LIGHT_CLI_MAX_TOKENS                    64

#define LIGHT_COMMAND_OPTION                    0
#define LIGHT_COMMAND_SWITCH                    1
struct light_command_option {
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
        struct light_command_option *option[LIGHT_COMMAND_MAX_OPTIONS];
        struct light_command *child[LIGHT_COMMAND_MAX_SUBCOMMANDS];
};
struct light_command_option_value {
        const struct light_command_option *option;
        const uint8_t *value;
};
struct light_cli_invocation {
        struct light_command *target;
        uint8_t option_count;
        struct light_command_option_value option[LIGHT_COMMAND_MAX_OPTIONS];
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

extern struct lobj_type ltype_light_command;
// static command max-args value is determined at load time by the size of .arg_name
#define Light_Command_Static(_name, _parent, _desc, _handler, _arg_min, _arg_max, ...) \
        { \
                /*   Static_RO, not plain RO: these live in file-scope storage the program did
                 * not allocate, and ltype_light_command carries a release hook that calls
                 * light_free(). Without the static flag, a put() reaching zero would hand that
                 * storage to free() -- see light_object_put_reg() */ \
                .header = Light_Object_Static_RO("light_cmd:"_name, NULL, &ltype_light_command), \
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
        Light_Command_Option_Type(name, command, LIGHT_COMMAND_SWITCH, code, description)

#define Light_Command_Option(command, code, name, description) \
        Light_Command_Option_Type(name, command, LIGHT_COMMAND_OPTION, code, description)

#define Light_Command_Declare(sym_name, parent) \
        extern struct light_command sym_name

extern void light_core__autoload_command(void *object);
extern void light_core__autoload_option(void *object);

//   NO __static_descriptor on the command itself. On RP2040/RP2350 that attribute expands to
// __in_flash(".descriptors"), and light_command_register() MUTATES the command at load
// time -- it writes parent, full_name, parent->child[] and parent->child_count. Those stores
// cannot land in read-only XIP flash, so the command has to sit in RAM. Every other port
// defines __static_descriptor as nothing, so this only ever mattered on RP2.
//
//   the autoload record below keeps __static_object: that one really is const, and belongs in
// the .light.static section the framework walks at init
#define Light_Command_Define(sym_name, parent, name, description, handler, _arg_min, _arg_max, ...) \
        struct light_command sym_name = \
                Light_Command_Static(name, parent, description, handler, _arg_min, _arg_max, __VA_ARGS__); \
        static const __static_object struct light_static_object autoload_## sym_name = \
                Light_Static_Object(&sym_name, light_core__autoload_command);

#define Light_Command_Option_Declare(sym_name, command) \
        extern struct light_command_option sym_name

// no __static_descriptor here either, and for the same reason as Light_Command_Define above:
// light_command_register_option() writes into the option's owning command, and an option in
// flash on RP2 could not participate
#define Light_Command_Option_Type_Define(sym_name, command, type, name, code, description) \
        struct light_command_option sym_name = \
                        Light_Command_Option_Type(name, command, type, code, description); \
        const struct light_static_object __static_object autoload_## sym_name = \
                Light_Static_Object(&sym_name, light_core__autoload_option)
#define Light_Command_Option_Define(sym_name, command, name, code, description) \
                Light_Command_Option_Type_Define(sym_name, command, LIGHT_COMMAND_OPTION, name, code, description)
#define Light_Command_Switch_Define(sym_name, command, name, code, description) \
                Light_Command_Option_Type_Define(sym_name, command, LIGHT_COMMAND_SWITCH, name, code, description)

extern struct light_command root_command;

// --- the application's root command -----------------------------------------------------------
//
//   PARENT SENTINEL, for a command that belongs at the top level of whatever application it is
// linked into. Pass it where Light_Command_Define() wants a parent:
//
//      Light_Command_Define(cmd_ui, LIGHT_COMMAND_APP_ROOT, "ui", "...", do_cmd_ui, 0, 0);
//
//   this exists because Light_Command_Define() resolves its parent at COMPILE time, from a
// symbol the defining translation unit can name. That is fine for an application defining its
// own subcommands, and impossible for a library module: light_ui cannot name cmd_crush or
// cmd_light_ui_demo, because which application it is being linked into is not knowable when
// light_ui is compiled. Before this, such a module had to keep its commands out of the autoload
// mechanism entirely and register them by hand once the application had named its tree.
//
//   the value is a non-null pointer that can never be a real command: address 1 is not
// dereferenceable and no object has it, so a parent field holding it is unambiguous. NULL was
// not available -- light_command_register() already reads that as "the global root_command"
#define LIGHT_COMMAND_APP_ROOT              ((struct light_command *)1)

//   how many commands may be waiting on the application root at once. NOT
// LIGHT_COMMAND_MAX_SUBCOMMANDS, which was the first guess and is wrong by a whole dimension: what
// waits here is every command DESCENDED from the sentinel, not one parent's children. crush has
// 26 commands and all but a handful of them are in that subtree.
//   overflowing it drops commands, and a dropped command is worse than an unregistered one --
// its children may still resolve and then walk a parent chain that still holds the sentinel. So
// this is sized to be comfortable rather than tight; it costs one pointer each, and only until
// the root is resolved.
#ifndef LIGHT_COMMAND_MAX_PENDING
#define LIGHT_COMMAND_MAX_PENDING  48
#endif

//   the root command emitted by Light_Application_Define(), or NULL in an application that has
// no command tree. Set when its autoload record is processed -- so nothing may assume it during
// static object loading, which is exactly why sentinel-parented commands are deferred rather
// than resolved on the spot (see light_core__autoload_command())
extern struct light_command *light_core_app_root_command;
static inline struct light_command *light_command_app_root(void)
{
        return light_core_app_root_command;
}

//   the default handler for that root: prints its own subcommand list. A root command exists to
// own subcommands, so "what can I type here" is the only sensible thing a bare invocation can
// mean -- and it is what the applications that hand-wrote a root were already doing.
extern struct light_cli_invocation_result light_core_app_root_help(struct light_cli_invocation *invoke);
extern void light_core__autoload_app_root(void *object);

//   RESOLVES THE APPLICATION ROOT and attaches everything parented to LIGHT_COMMAND_APP_ROOT.
// Deliberately not done as the records arrive: whether the auto-declared root should be
// registered at all depends on whether the application also declares one by hand, and that is
// not known until every static object has loaded. light_cli calls this from its APP_LAUNCH hook,
// ahead of parsing the command line; a host test that inits the framework without running it
// calls it directly
extern void light_core__resolve_app_root(void);

//   THE NAME THE ROOT COMMAND TAKES, which defaults to the application's own name and is
// overridden by defining this before including light_cli.h -- the same arrangement, and for the
// same reason, as LIGHT_APP_VERSION in light.h: Light_Application_Define()'s variadic tail is
// the module dependency list, so a name argument could only go in front of it and every call
// site across four repositories would have to change together.
//   the override is not hypothetical. screen-test's HUSB238 application is defined as
// `screentest_husb238` and its command line has always been `husb238`; deriving the root name
// from the application name alone would rename it.
#ifndef LIGHT_APP_ROOT_COMMAND_DESCRIPTION
#define LIGHT_APP_ROOT_COMMAND_DESCRIPTION      "application root command"
#endif

//   DECLARES AN APPLICATION'S ROOT COMMAND, invoked by Light_Application_Define() in light.h.
// This is why the whole file lives in light_core: the application macro has to be able to
// expand it, and light_core cannot depend on light_cli to supply it.
//
//   THE NAME IS THE APPLICATION'S OWN SYMBOL, always, with nothing to configure. That symbol is
// already the identity of the application everywhere else -- it names the module (mod_##name),
// the application object, and the linker sections the framework walks -- and it is unique in a
// binary by construction, since two applications defining the same symbol would not link. So
// the root command inherits a name that cannot collide and cannot be forgotten.
//   there was briefly a LIGHT_APP_ROOT_COMMAND_NAME override, for applications whose command
// line was spelled differently from their symbol. It bought a name one line shorter at the cost
// of a rule about include order -- the override had to precede light.h or it was silently
// ignored, leaving the root named after the application anyway. Deriving it is what makes that
// class of mistake impossible.
//   the last item deliberately has no trailing semicolon -- the Light_Application_Define() call
// site supplies it.
#define Light_Application_Root_Command(_name) \
        struct light_command light_core_app_root_storage = \
                Light_Command_Static(#_name, &root_command, \
                        (const uint8_t *)LIGHT_APP_ROOT_COMMAND_DESCRIPTION, \
                        light_core_app_root_help, 0, 0); \
        static const __static_object struct light_static_object autoload_light_core_app_root = \
                Light_Static_Object(&light_core_app_root_storage, light_core__autoload_app_root)

static inline const uint8_t *light_command_get_full_name(struct light_command *command)
{
        return command->full_name;
}
static inline const struct light_command *light_command_get_parent(struct light_command *command)
{
        return command->parent;
}
static inline const uint8_t *light_command_get_object_id(struct light_command *command)
{
        return light_object_get_name(&command->header);
}
static inline const uint8_t *light_command_get_short_name(struct light_command *command)
{
        return command->short_name;
}
static inline const uint8_t *light_command_get_description(struct light_command *command)
{
        return command->description;
}

static inline const char light_command_option_get_code(struct light_command_option *option)
{
        return option->code;
}
static inline const uint8_t *light_command_option_get_name(struct light_command_option *option)
{
        return option->name;
}
static inline uint8_t light_command_option_get_type(struct light_command_option *option)
{
        return option->type;
}
static inline const uint8_t *light_command_option_get_description(struct light_command_option *option)
{
        return option->description;
}

extern struct light_command_option *light_command_find_option(
                                struct light_command *command, const uint8_t *name);
static inline struct light_command_option *light_command_find_option_root(const uint8_t *name)
{
        return light_command_find_option(NULL, name);
}
// command and option API
extern struct light_command *light_command_create(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                struct light_cli_invocation_result (*handler)(struct light_cli_invocation *));
extern void light_command_register(
                                struct light_command *parent,
                                struct light_command *command);

extern struct light_command_option *light_command_create_option(
                                struct light_command *parent,
                                const uint8_t code,
                                const uint8_t *name,
                                bool arg,
                                const uint8_t *description);

static inline struct light_command_option *light_command_create_option_root(
                                const uint8_t code,
                                const uint8_t *name,
                                bool arg,
                                const uint8_t *description)
{
        return light_command_create_option(NULL, code, name, arg, description);
}
extern void light_command_register_option(
                                struct light_command *parent,
                                struct light_command_option *option);

static inline void light_command_register_option_root(
                                struct light_command_option *option)
{
        return light_command_register_option(NULL, option);
};
static inline struct light_command_option *light_command_create_switch(
                                struct light_command *parent,
                                const uint8_t code,
                                const uint8_t *name,
                                const uint8_t *description)
{
        return light_command_create_option(parent, code, name, false, description);
}
static inline struct light_command_option *light_command_create_switch_root(
                                const uint8_t code,
                                const uint8_t *name,
                                const uint8_t *description)
{
        return light_command_create_switch(NULL, code, name, description);
}
extern struct light_command *light_command_find(
                                struct light_command *parent, const uint8_t *name);

//   writes a usage summary for `command` -- its own usage line and description, then its
// subcommands and its options -- to light_stream_stdout. Through the stream layer rather than
// printf so it stays ordered with everything else the command tree logs, and so it works on a
// target whose console is not stdio at all
extern void light_command_print_help(struct light_command *command);

#endif