#ifndef _LIGHT_CORE_H
#define _LIGHT_CORE_H

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#include <light_core_port.h>
#include <light_object.h>
#include <light_stream.h>
#include <light_common.h>
#include <light_platform.h>

//   derived from git tags at build time -- see cmake/light_version.cmake and
// scripts/light-version.ps1. Generated into the build tree rather than committed, so it cannot
// drift from the commit it describes; a build with no git available reports "0.0.0-unknown"
// rather than failing to compile.
#include <light_version.h>
#define LF_VERSION_STR                  LIGHT_VERSION_STRING

#define LF_INFO_STR                     "Light Framework v" LF_VERSION_STR ", " LIGHT_BUILD_STRING

#define LF_EVENT_MODULE_LOAD            0
#define LF_EVENT_MODULE_UNLOAD          1
#define LF_EVENT_APP_LAUNCH             2
#define LF_EVENT_APP_SHUTDOWN           3

#define LF_STATIC_MODULES_MAX           16

#define LF_MODULE_DEPS_MAX              32

#define LF_STATUS_RUN                   0
#define LF_STATUS_SHUTDOWN              1
#define LF_STATUS_ERROR                 2

// include "light_conf.h

#ifndef LIGHT_CONF_TASKS_MAX
#define LIGHT_CONF_TASKS_MAX            8
#endif

#define LF_TASKS_MAX                    LIGHT_CONF_TASKS_MAX

struct light_module {
        struct light_object header;
        uint8_t module_deps_count;
        const struct light_module *module_deps[LF_MODULE_DEPS_MAX];
        void (*event)(const struct light_module *, uint8_t, void*);
};

struct light_application {
        struct light_object header;
        struct light_module module;
        //   the application's own version, distinct from the framework's. NULL when the
        // application never declared one, which is the default and is reported honestly rather
        // than being passed off as a version
        const char *version;
        void (*event)(const struct light_module *, uint8_t, void*);
        uint8_t (*app_main)(struct light_application *);
};

//   an application declares its version by defining this before including light.h, or -- more
// usefully -- by having its build define it, which is how it can carry a git-derived string
// without anything being committed. cmake/light_version.cmake generates exactly such a value.
//
//   deliberately NOT a parameter of Light_Application_Define(). That macro's variadic tail is
// the module dependency list, so a version argument could only go in front of it, and there are
// around twenty call sites across four repositories -- several inside submodules checked out by
// two parents each, which would need coordinated pointer bumps. A macro with a default costs
// nothing at the call sites and lets a build supply the value per target, which is where it
// should come from anyway
#ifndef LIGHT_APP_VERSION
#define LIGHT_APP_VERSION               NULL
#endif

struct light_event_app_launch {
        int argc;
        char **argv;
};

struct light_static_object {
        void *target;
        void (*load)(void *);
};
#define Light_Static_Object(_target, _load) \
{ \
        .target = _target, \
        .load = _load \
}

#define Light_Module(_name, _event, ...) \
{ \
        .header = Light_Object_RO(_name, NULL, &ltype_light_module), \
        .event = _event, \
        .module_deps = { __VA_ARGS__ } \
}
#define Light_Module_Static(_name, _event, ...) \
{ \
        .header = Light_Object_Static_RO(_name, NULL, &ltype_light_module), \
        .event = _event, \
        .module_deps = { __VA_ARGS__ } \
}

#define Light_Module_Declare(name) \
        extern const struct light_module name

#define Light_Module_Define(name, event, ...) \
        const struct light_module __static_descriptor name = Light_Module_Static(#name, event, __VA_ARGS__); \
        static const struct light_module __static_module *this_module = &name

#define Light_Application(_name, _event, _main, ...) \
{ \
        .header = Light_Object_Static_RO(_name, NULL, &ltype_light_application), \
        .module = Light_Module_Static(_name, _lf_app_event, __VA_ARGS__), \
        .version = LIGHT_APP_VERSION, \
        .event = _event, \
        .app_main = _main \
}

//   `this_module` below is `static`, never referenced, and exists only to be FOUND BY A
// LINKER-SECTION WALK -- _find_static_modules() iterates .light.static_module between
// __light_modules_start and __light_modules_end. Nothing in C refers to it, so a compiler is
// entitled to delete it, and at -O1 and above GCC does exactly that. The section attribute
// does not prevent this; only `used` does, which is why every port's __static_module carries
// it (see any light_core_port.h).
//   This was silently broken on RP2040/RP2350 for as long as they have built at -Og: the
// touch board reported "located 0 static modules" while the same source found 2 on a target
// that happened to build at -O0. Light_Stream_Define() escaped it only because its pointer has
// EXTERNAL linkage, which the compiler must emit -- which is why .light.stream was populated
// in the very same images where .light.module was empty.
//   THE COMMAND TREE, which an application's own definition below depends on.
//
//   every application that has a command line needs a root command named after itself: the
// parser takes argv[0] as that root (see light_cli's handle_command_line()), so the name has to
// match what the program is invoked as. Three applications wrote that declaration out by hand
// and a fourth would have too -- and, worse, a LIBRARY module wanting to contribute a top-level
// subcommand had nowhere to attach it, because Light_Command_Define() names its parent at
// compile time and a library cannot name the application's symbol. LIGHT_COMMAND_APP_ROOT solves
// that, and Light_Application_Root_Command() below is what it resolves to.
//
//   INCLUDED HERE, and here specifically: light_command.h builds on light_object and the static
// object machinery defined above, and Light_Application_Define() immediately below expands
// Light_Command_Static(). Both constraints are ordering constraints on this one line.
//
//   this used to live in light_cli.h, reached through a macro hook that light_cli overrode.
// That was backwards -- it made light_core's application macro depend on a module that depends
// on light_core -- so the tree moved down here where the rest of the framework's registries
// live, and light_cli kept the command LINE: the tokenizer, the parser, dispatch and the queue.
#include <light_command.h>

#define Light_Application_Define(name, event, main, ...) \
        static struct light_application __this_app = Light_Application(#name, event, main, __VA_ARGS__); \
        static struct light_module __static_module *this_module = &__this_app.module; \
        struct light_module *mod_ ## name = &__this_app.module; \
        struct light_application *this_app = &__this_app; \
        Light_Application_Root_Command(name)

#define to_module(ptr) container_of(ptr, struct light_module, header)
#define to_application(ptr) container_of(ptr, struct light_application, header)
#define module_to_application(ptr) container_of(ptr, struct light_application, module)

Light_Module_Declare(light_core);

// at least for now this symbol is a single global value determined at build-time
extern struct light_application *this_app;

extern struct lobj_type ltype_light_module;
extern struct lobj_type ltype_light_application;

extern void _lf_app_event(const struct light_module *module, uint8_t event, void *arg);

// framework entry point, enumerates module dependency graph and loads required modules
extern void light_framework_init();
// runs the loaded application to completion, returning LF_STATUS_SHUTDOWN for a clean run or
// LF_STATUS_ERROR if any task reported failure -- which is how a command-line application
// finds out whether its command actually succeeded.
//
// a framework status rather than a process exit code on purpose: only some applications have
// a process to exit from, and mapping one to the other is theirs to decide. embedded callers
// that never return can carry on ignoring it
extern uint8_t light_framework_run(int argc, char *argv[]);
extern void light_framework_load_application(struct light_application *app);
extern void light_framework_load_module(const struct light_module *mod);
extern struct light_application *light_framework_get_root_application();
extern const char *light_task_status_string(uint8_t status);

extern void light_module_register_periodic_task(const struct light_module *module,
                                                const uint8_t *name,
                                                uint8_t (*task)(struct light_application *));
extern void light_module_unregister_periodic_task(const struct light_module *module,
                                                uint8_t (*task)(struct light_application *));
extern void light_module_register_one_shot_task(const struct light_module *module,
                                                const uint8_t *name,
                                                uint8_t (*task)(struct light_application *));
static inline const char *light_application_get_name(struct light_application *app)
{
        return app->header.id;
}
static inline struct light_module *light_application_get_main_module(struct light_application *app)
{
        return &app->module;
}
static inline const char *light_module_get_name(const struct light_module *mod)
{
        return mod->header.id;
}

#define _light_module_do_event_send(_module, _event, _arg) _module->event(_module, _event, _arg)
#define light_module_event_send(_module, _event, _arg) do { \
        light_debug("sending event [%s] to module [%s]", #_event, light_module_get_name(_module)); \
        _light_module_do_event_send(_module, _event, _arg); \
} while(0)

extern void _light_module_event_do_send_to_all(uint8_t event, void *arg);

#define light_module_event_send_to_all(event, arg) do { \
        light_debug("sending event [%s] to all modules...", #event); \
        _light_module_event_do_send_to_all(event, (void *)arg); \
} while(0)

#endif