#include <light.h>
#include <module/mod_light_ioport.h>

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_ioport, _module_event,
                                &light_core);

static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
}