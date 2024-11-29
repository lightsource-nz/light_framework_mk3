#include <light.h>
#include <module/mod_light_cli.h>

static void demo_cli_app_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t demo_cli_main(struct light_application *app);

Light_Application_Define(demo_cli, demo_cli_app_event, demo_cli_main,
                                                                &light_cli,
                                                                &light_core);

void main(int argc, char *argv[])
{
        light_framework_init();
        light_framework_run(argc, argv);
}

static void demo_cli_app_event(const struct light_module *mod, uint8_t event, void *arg)
{
        switch (event) {
        case LF_EVENT_APP_LAUNCH:
                light_info("demo app received LAUNCH event","");
                break;
        case LF_EVENT_APP_SHUTDOWN:
                light_info("demo app received SHUTDOWN event","");
                break;
        }
}

static uint8_t demo_cli_main(struct light_application *app)
{
        return LF_STATUS_SHUTDOWN;
}
