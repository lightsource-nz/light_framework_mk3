#include <light.h>
#include <module/mod_light_display_ioport.h>

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_display_ioport, _module_event,
                                &light_core);

static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
}