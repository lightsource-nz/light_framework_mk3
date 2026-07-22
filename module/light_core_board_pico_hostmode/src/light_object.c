/*
 *  light_object.c
 *  core definitions for the light object model
 * 
 *  authored by Alex Fulton
 *  created december 2024
 * 
 */
#include <light.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>

static bool _registry_loaded = false;
static struct light_object_registry _registry_default;

void light_core_impl_setup()
{
        if(!_registry_loaded) {
                _registry_default.alloc = light_alloc;
                _registry_default.free = light_free;
                _registry_loaded = true;
        }
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
        atomic_store(&obj->ref_count, 1);
        obj->type = type;
}
// TODO implement saturation conditions and warnings
struct light_object *light_object_get_reg(struct light_object_registry *reg, struct light_object *obj)
{
        struct light_object *ref = obj;
        if(obj) {
                uint32_t old;
                do {
                        old = obj->ref_count;
                        if(old == 0) {
                                ref = NULL;
                                break;
                        }
                } while (!atomic_compare_exchange_strong(&obj->ref_count, &old, obj->ref_count + 1));
        }
        return ref;
}
void light_object_put_reg(struct light_object_registry *reg, struct light_object *obj)
{
        uint8_t status;
        uint32_t count = obj->ref_count;
        do {
                if(count > 0)
                        status = atomic_compare_exchange_strong(&obj->ref_count, &count, count - 1);
                else
                        return;
        } while (status);
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
