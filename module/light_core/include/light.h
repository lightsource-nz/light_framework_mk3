#ifndef _LIGHT_CORE_H
#define _LIGHT_CORE_H

#include <light_common.h>
#include <light_core_port.h>
#include <light_object.h>
#include <light_platform.h>

// TODO implement version fields properly
#define LF_VERSION_STR                  "0.1.0"

#define LF_INFO_STR                     "Light Framework v" LF_VERSION_STR ", " LIGHT_BUILD_STRING

#define LF_EVENT_MODULE_LOAD            0
#define LF_EVENT_MODULE_UNLOAD          1
#define LF_EVENT_APP_LOAD               2
#define LF_EVENT_APP_UNLOAD             3

#define LF_STATIC_MODULES_MAX           16

#define LF_MODULE_DEPS_MAX              8

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
        void (*event)(const struct light_module *, uint8_t, void*);
        uint8_t (*app_main)(struct light_application *);
};

struct light_event_app_load {
        int argc;
        char **argv;
};

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

#define __static_module __attribute__ ((section(".light.modules")))

#define Light_Module_Declare(name) \
        extern const struct light_module name;

#define Light_Module_Define(name, event, ...) \
        const struct light_module __static_descriptor name = Light_Module_Static(#name, event, __VA_ARGS__); \
        static const struct light_module __static_module *this_module = &name;

#define Light_Application(_name, _event, _main, ...) \
{ \
        .header = Light_Object_Static_RO(_name, NULL, &ltype_light_application), \
        .module = Light_Module_Static("mod_" _name, _lf_app_event, __VA_ARGS__), \
        .event = _event, \
        .app_main = _main \
}

#define Light_Application_Define(name, event, main, ...) \
        static struct light_application __this_app = Light_Application(#name, event, main, __VA_ARGS__); \
        static struct light_module __static_module *this_module = &__this_app.module; \
        struct light_module *mod_ ## name = &__this_app.module; \
        struct light_application *this_app = &__this_app;

#define to_module(ptr) container_of(ptr, struct light_module, header)
#define to_application(ptr) container_of(ptr, struct light_application, header)
#define module_to_application(ptr) container_of(ptr, struct light_application, module)

Light_Module_Declare(light_core)

// at least for now this symbol is a single global value determined at build-time
extern struct light_application *this_app;

extern struct lobj_type ltype_light_module;
extern struct lobj_type ltype_light_application;

extern void _lf_app_event(const struct light_module *module, uint8_t event, void *arg);

// framework entry point, enumerates module dependency graph and loads required modules
extern void light_framework_init(int argc, char *argv[]);
extern void light_framework_run();
extern void light_framework_load_application(
                        struct light_application *app, int argc, char *argv[]);
extern void light_framework_load_module(const struct light_module *mod);
extern struct light_application *light_framework_get_root_application();
extern const char *light_task_status_string(uint8_t status);

extern void light_module_register_periodic_task(const struct light_module *module,
                                                const uint8_t *name,
                                                uint8_t (*task)(struct light_application *));
extern void light_module_unregister_periodic_task(const struct light_module *module,
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

#define _light_module_do_event_send(_module, _event, _arg) _module->event(_module, _event, _arg);
#define light_module_event_send(_module, _event, _arg) do { \
        light_trace("sending event '%s' to module '%s'", #_event, light_module_get_name(_module)); \
        _light_module_do_event_send(_module, _event, _arg); \
} while(0)

extern void _light_module_event_do_send_to_all(uint8_t event, void *arg);

#define light_module_event_send_to_all(event, arg) do { \
        light_debug("sending event [%d] to all modules..."); \
        _light_module_event_do_send_to_all(event, (void *)arg); \
} while(0)

#endif