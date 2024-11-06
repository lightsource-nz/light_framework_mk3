#include <light.h>

static void framework_demo_app_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t framework_demo_main(struct light_application *app);

Light_Application_Define(framework_demo, framework_demo_app_event, framework_demo_main, &light_core);

void main(int argc, char *argv[])
{
        light_framework_init(argc, argv);
        light_framework_run();
}

static void framework_demo_app_event(const struct light_module *mod, uint8_t event, void *arg)
{
        switch (event) {
        case LF_EVENT_APP_LOAD:
                light_info("demo app received LOAD event","");
                break;
        case LF_EVENT_APP_UNLOAD:
                light_info("demo app received UNLOAD event","");
                break;
        }
}

static uint8_t framework_demo_main(struct light_application *app)
{
        return LF_STATUS_SHUTDOWN;
}
