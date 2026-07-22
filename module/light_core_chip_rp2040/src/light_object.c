/*
 *  light_object_rp2040.c
 *  core definitions for the light object model
 * 
 *  authored by Alex Fulton
 *  created june 2024
 * 
 */
#include <light.h>

#include <pico/multicore.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static volatile bool _worker_stop_requested;
static volatile bool _worker_finished;

void light_core_port_worker_launch(void (*worker_fn)(void))
{
        _worker_stop_requested = false;
        _worker_finished = false;
        multicore_launch_core1(worker_fn);
}
void light_core_port_worker_signal_stop(void)
{
        _worker_stop_requested = true;
}
bool light_core_port_worker_stop_requested(void)
{
        return _worker_stop_requested;
}
void light_core_port_worker_signal_finished(void)
{
        _worker_finished = true;
}
void light_core_port_worker_join(void)
{
        while(!_worker_finished) {
                tight_loop_contents();
        }
}

static bool _registry_loaded = false;
static struct light_object_registry _registry_default;

static void _registry_critical_enter(struct light_object_registry *reg)
{
        critical_section_enter_blocking(&reg->mutex);
}
static void _registry_critical_exit(struct light_object_registry *reg)
{
        critical_section_exit(&reg->mutex);
}

void light_core_impl_setup()
{
        if(!_registry_loaded) {
                _registry_loaded = true;
                critical_section_init(&_registry_default.mutex);
                _registry_default.alloc = light_alloc;
                _registry_default.free = light_free;
        }
}
// no OS scheduler to block on here -- see light_condition_t in light_core_port.h for why a
// spin-poll on a plain flag is safe. must be called with 'mutex' already held, per the
// light_condition_wait() contract used throughout light_core/src/stream.c
void light_core_port_condition_wait(light_condition_t *cond, light_mutex_t *mutex)
{
        light_mutex_do_unlock(mutex);
        while(!*cond) {
                tight_loop_contents();
        }
        *cond = false;
        light_mutex_do_lock(mutex);
}
static struct light_object_registry *_get_default_registry()
{
        if(_registry_loaded)
                return &_registry_default;
        return NULL;
}
extern struct light_object_registry *light_object_registry_default()
{
        return _get_default_registry();
}

extern struct light_object *light_object_get(struct light_object *obj)
{
        return light_object_get_reg(_get_default_registry(), obj);
}
extern void light_object_put(struct light_object *obj)
{
        light_object_put_reg(_get_default_registry(), obj);
}

extern void light_object_init(struct light_object *obj, const struct lobj_type *type)
{
        light_object_init_reg(_get_default_registry(), obj, type);
}

extern void *light_object_alloc(size_t size)
{
        return light_object_alloc_reg(_get_default_registry(), size);
}
extern void *light_object_alloc_reg(struct light_object_registry *reg, size_t size)
{
        return reg->alloc(size);
}
extern void light_object_free(void *obj)
{
        light_object_free_reg(_get_default_registry(), obj);
}
extern void light_object_free_reg(struct light_object_registry *reg, void *obj)
{
        reg->free(obj);
}

static uint8_t light_object_set_name_va(struct light_object *obj, const uint8_t *format, va_list vargs)
{
        if(!format || !format[0]) {
                // TODO log empty name field error
                return LIGHT_INVALID;
        }
        vsnprintf(obj->id, LIGHT_OBJ_NAME_LENGTH, format, vargs);
        return LIGHT_OK;
}
static int light_object_add_internal(struct light_object_registry *reg, struct light_object *obj)
{
        struct light_object *parent = light_object_get(obj->parent);

        if(parent && parent->type->evt_child_add)
                parent->type->evt_child_add(parent, obj);
        if(obj->type->evt_add)
                obj->type->evt_add(obj, parent);

        return LIGHT_OK;
}
int light_object_add_va_reg(struct light_object_registry *reg, struct light_object *obj, struct light_object *parent,
                               const uint8_t *format, va_list vargs)
{
        int retval;

        retval = light_object_set_name_va(obj, format, vargs);

        if(retval) {
            light_warn("Could not set name of object at 0x%X: %s\n", obj, light_error_to_string(retval));
            return retval;
        }

        obj->parent = parent;
        return light_object_add_internal(reg, obj);
}
int light_object_add(struct light_object *obj, struct light_object *parent,
                            const uint8_t *format, ...)
{
        va_list vargs;

        va_start(vargs, format);
        return light_object_add_va(obj, parent, format, vargs);
        va_end(vargs);
}
int light_object_add_va(struct light_object *obj, struct light_object *parent,
                            const uint8_t *format, va_list vargs)
{
        return light_object_add_va_reg(&_registry_default, obj, parent, format, vargs);
}
int light_object_del(struct light_object *obj)
{
        light_object_del_reg(&_registry_default, obj);
}
int light_object_del_reg(struct light_object_registry *reg, struct light_object *obj)
{
        light_object_put_reg(reg, obj->parent);
        if(obj->parent->type->evt_child_remove)
                obj->parent->type->evt_child_remove(obj->parent, obj);
        obj->parent = NULL;
}

void light_object_init_reg(struct light_object_registry *reg, struct light_object *obj, const struct lobj_type *type)
{
        obj->ref_count = 1;
        obj->type = type;
}
// TODO implement saturation conditions and warnings
struct light_object *light_object_get_reg(struct light_object_registry *reg, struct light_object *obj)
{
        struct light_object *ref = obj;
        if(obj) {
                critical_section_enter_blocking(&reg->mutex);
                if(obj->ref_count > 0)
                        obj->ref_count++;
                else
                         ref = NULL;
                critical_section_exit(&reg->mutex);
        }
        return ref;
}
void light_object_put_reg(struct light_object_registry *reg, struct light_object *obj)
{
        critical_section_enter_blocking(&reg->mutex);
        obj->ref_count--;
        critical_section_exit(&reg->mutex);
}

int light_object_add_reg(struct light_object_registry *reg, struct light_object *obj, struct light_object *parent,
                            const uint8_t *format, ...)
{
        va_list vargs;
        int retval;

        va_start(vargs, format);
        retval = light_object_add_va_reg(reg, obj, parent, format, vargs);
        va_end(vargs);

        return retval;
}

int light_ref_get(light_ref_t *ref)
{

}
void light_ref_put(light_ref_t *ref)
{

}
