#include <light.h>

#include "framework_internal.h"

#define LTYPE_LIGHT_MODULE_ID "light.module"
#define LTYPE_LIGHT_APPLICATION_ID "light.application"

#define MODULE_ID_LIGHT_CORE "mod_light_core"

#define _STATUS_STRING_RUN         "run"
#define _STATUS_STRING_SHUTDOWN    "shutdown"
#define _STATUS_STRING_ERROR       "error"

extern int __light_modules_start, __light_modules_end;

static struct light_module **static_modules;
static uintptr_t static_module_count;

static uint8_t available_module_count;
static const struct light_module *available_modules[LF_STATIC_MODULES_MAX];

struct light_periodic {
        const struct light_module *owner;
        const uint8_t *name;
        uint8_t (*run)(struct light_application *);
};

static uint8_t app_task_count = 0;
static struct light_periodic app_tasks[LF_TASKS_MAX];

// TODO need to properly init reference counts in object headers of static modules
static void _find_static_modules()
{
        light_trace("&__light_modules_start=0x%x, &__light_modules_end=0x%x, sizeof(void *)=0x%x", &__light_modules_start, &__light_modules_end, sizeof(void *));
        light_trace("((_start - _end = 0x%x) / 0x%x)=0x%x",((uintptr_t)&__light_modules_end) - (uintptr_t)&__light_modules_start, sizeof(void *), (((uintptr_t)&__light_modules_end) - ((uintptr_t)&__light_modules_start)) / sizeof(void *));
        static_modules = (struct light_module **) &__light_modules_start;
        static_module_count = (((uintptr_t)&__light_modules_end) - ((uintptr_t)&__light_modules_start)) / sizeof(void *);
        light_debug("located %d static modules", static_module_count);
}
static void _module_release(struct light_object *obj)
{
        light_free(to_module(obj));
}
static void _application_release(struct light_object *obj)
{
        light_free(to_application(obj));
}
struct lobj_type ltype_light_module = {
        .id = LTYPE_LIGHT_MODULE_ID,
        .release = _module_release
};
struct lobj_type ltype_light_application = {
        .id = LTYPE_LIGHT_APPLICATION_ID,
        .release = _application_release
};

static void _event_app_launch(const struct light_module *module, struct light_event_app_launch *event)
{
        
}
static void _event_module_load(const struct light_module *module)
{
        
}
static void _event_module_unload(const struct light_module *module)
{
        
}
static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
        case LF_EVENT_APP_LAUNCH:
                _event_app_launch(module, (struct light_event_app_launch *)arg);
                break;
        case LF_EVENT_MODULE_LOAD:
                _event_module_load(module);
                break;
        case LF_EVENT_MODULE_UNLOAD:
                _event_module_unload(module);
                break;
        }
}
Light_Module_Define(light_core,_module_event,);

// framework loading progress flags
static uint8_t framework_loading = 0;
static uint8_t framework_loaded = 0;
static uint8_t application_loading = 0;
static uint8_t application_loaded = 0;
// framework internal state variables
static struct light_application *root_application = NULL;
static uint8_t mods_loading_count = 0;
static uint8_t mods_active_count = 0;
static struct light_module *mods_loading[LF_STATIC_MODULES_MAX];
static struct light_module *mods_active[LF_STATIC_MODULES_MAX];

static uint8_t _lf_app_task(struct light_application *app)
{
        light_debug("Calling main task for application '%s', system time =%ums",
                                                        light_application_get_name(app),
                                                        light_platform_get_time_since_init());

        return app->app_main(app);
}

void _lf_app_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
        case LF_EVENT_MODULE_LOAD:
                light_module_register_periodic_task(module,"lf_app_main", _lf_app_task);
                break;
        case LF_EVENT_MODULE_UNLOAD:
                light_module_unregister_periodic_task(module, _lf_app_task);
                break;
        }

        application_loading = 1;
        // pass event to app-level handler
        this_app->event(module, event, arg);
        application_loaded = 1;
}
void light_framework_init()
{
        light_common_init();
        light_platform_init();
        light_info("Loading Light Framework runtime...", "");
        light_info("%s", LF_INFO_STR);

        light_core_impl_setup();
        framework_loading = 1;
        _find_static_modules();

        root_application = this_app;
        light_framework_load_application(root_application);
        framework_loaded = 1;
}
 
// begin execution of all loaded applications and periodic tasks
void light_framework_run(int argc, char *argv[])
{
        // first we send all modules the APP_LAUNCH event...
        struct light_event_app_launch event = {
                .argc = argc, .argv = argv
        };
        light_module_event_send_to_all(LF_EVENT_APP_LAUNCH, &event);

        // ...then we begin scheduling application tasks
        struct light_application *app = light_framework_get_root_application();
        uint8_t status = LF_STATUS_RUN;
        struct light_periodic *task;
         while(status == LF_STATUS_RUN) {
                for(uint8_t i = 0; i < app_task_count && status == LF_STATUS_RUN; i++) {
                        task = &app_tasks[i];
                        status = task->run(app);
                }
        }
        light_debug("task %s returned code %s, shutting down",
                                        task->name, light_task_status_string(status));
}

void light_framework_load_application(struct light_application *app)
{
        if(!framework_loading)
                light_fatal("attempted to load an application before calling light_framework_init()","");
        // TODO verify at build-time that we support the runtime version requested by this app
        light_info("loading application '%s': app version %s, framework version %s",
                                        light_application_get_name(app),"NULL",LF_VERSION_STR);
        // this call recursively loads the entire module tree for the application
        light_framework_load_module(light_application_get_main_module(app));

        light_info("application '%s' loaded successfully", light_application_get_name(app));
}
// TODO overhaul arraylist API to make it suck less
void light_framework_load_module(const struct light_module *mod)
{
        light_trace("begin loading module %s", light_module_get_name(mod));
        light_arraylist_append(&mods_loading, &mods_loading_count, mod);

        // make sure all dependency modules are loaded before activating
        for(uint8_t i = 0; mod->module_deps[i] != NULL && i < LF_MODULE_DEPS_MAX; i++) {
                if(light_arraylist_indexof(&mods_loading, mods_loading_count, mod->module_deps[i]) == -1) {
                        light_framework_load_module(mod->module_deps[i]);
                }
        }
        // send MODULE_LOAD event; module is activated once it returns
        light_module_event_send(mod, LF_EVENT_MODULE_LOAD, NULL);
        light_arraylist_append(&mods_active, &mods_active_count, mod);
        light_trace("module %s loaded successfully", light_module_get_name(mod));
}

void light_module_register_periodic_task(const struct light_module *module,
                                                const uint8_t *name,
                                                uint8_t (*task)(struct light_application *))
{
        if(!(app_task_count < LF_TASKS_MAX)) {
                light_warn("Failed to register periodic task for module '%s', module may not work correctly", light_module_get_name(module));
                return;
        }
        app_tasks[app_task_count++] = (struct light_periodic) { module, name, task };
}
void light_module_unregister_periodic_task(const struct light_module *module,
                                                uint8_t (*task)(struct light_application *))
{
        //light_arraylist_delete_item(&app_tasks, &app_task_count, task);
}
struct light_application *light_framework_get_root_application()
{
        return root_application;
}
extern void _light_module_event_do_send_to_all(uint8_t event, void *arg)
{
        if(framework_loading)
        for(uint8_t i = 0; i < mods_active_count; i++) {
                _light_module_do_event_send(mods_active[i], event, arg);
        }
}
const char *light_task_status_string(uint8_t status)
{
        switch (status)
        {
        case LF_STATUS_RUN:
                return _STATUS_STRING_RUN;
        case LF_STATUS_SHUTDOWN:
                return _STATUS_STRING_SHUTDOWN;
        case LF_STATUS_ERROR:
                return _STATUS_STRING_ERROR;
        
        default:
                return "undefined";
        }
}
