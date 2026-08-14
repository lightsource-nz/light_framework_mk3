/*
 *  light_core_board_host_os/src/light_object.c
 *  core definitions for the light object model
 * 
 *  authored by Alex Fulton
 *  created november 2024
 * 
 */

#include <light.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>

static bool _registry_loaded = false;
static struct light_object_registry _registry_default;

//   these guard the object TREE only. Reference counts here are lock-free compare-exchange
// loops, so get() and put() take no lock at all and are not serialised by this.
//
//   That is also why, unlike the target ports, code inside one of these sections MAY call
// light_object_get()/put(): those touch no lock, so there is nothing to nest and nothing to
// deadlock against. User callbacks are still fired outside the section -- a callback is free
// to call light_object_add(), which does take this lock, and mtx_plain is not re-entrant
static void _registry_critical_enter(struct light_object_registry *reg)
{
        light_mutex_do_lock(&reg->mutex);
}
static void _registry_critical_exit(struct light_object_registry *reg)
{
        light_mutex_do_unlock(&reg->mutex);
}

void light_core_impl_setup()
{
        if(!_registry_loaded) {
                light_mutex_init(&_registry_default.mutex);
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
//   callbacks only, and called with the tree lock RELEASED: a callback is free to call
// light_object_add(), which takes that lock, and mtx_plain is not re-entrant. Device libraries
// hang their initialisation off evt_add, so that is a live path rather than a hypothetical.
//
//   `parent` is the reference already taken by the caller, not a fresh lookup: re-reading
// obj->parent here would race with a concurrent del() that has detached the object since
static int light_object_add_internal(struct light_object_registry *reg, struct light_object *obj,
                                        struct light_object *parent)
{
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

        //   the link and the reference on the parent are established in ONE section, so no
        // other thread can observe the object attached but not yet holding its reference --
        // the window in which a concurrent del() would detach it and drop a reference that had
        // not been taken. light_object_get() is safe to call in here: it is compare-exchange,
        // not a lock. Note a dead parent leaves the link set but yields no reference, which is
        // the behaviour this has always had
        _registry_critical_enter(reg);
        obj->parent = parent;
        parent = light_object_get(parent);
        _registry_critical_exit(reg);

        return light_object_add_internal(reg, obj, parent);
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
        return light_object_del_reg(&_registry_default, obj);
}
int light_object_del_reg(struct light_object_registry *reg, struct light_object *obj)
{
        struct light_object *parent;

        //   read the link and clear it in one section, so two threads detaching the same
        // object cannot both come away with the same parent and release it twice. Whoever
        // loses sees NULL and does nothing
        _registry_critical_enter(reg);
        parent = obj->parent;
        obj->parent = NULL;
        _registry_critical_exit(reg);

        if(!parent)
                return LIGHT_OK;

        //   callback first, put second. The put can drop the last reference, and this used to
        // run the other way round -- releasing the parent and then handing it to the callback,
        // which is a use-after-free whenever that was the final reference
        if(parent->type->evt_child_remove)
                parent->type->evt_child_remove(parent, obj);
        light_object_put_reg(reg, parent);

        return LIGHT_OK;
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
                //   the desired value is old + 1, NOT a fresh read of ref_count: a second read
                // can observe an unrelated value, and if the count then returns to `old` the
                // exchange commits that unrelated value instead of an increment. The exchange
                // failing is what refreshes `old`, so retry on failure and stop on success
                uint32_t old = atomic_load(&obj->ref_count);
                do {
                        if(old == 0) {
                                ref = NULL;
                                break;
                        }
                } while (!atomic_compare_exchange_weak(&obj->ref_count, &old, old + 1));
        }
        return ref;
}
void light_object_put_reg(struct light_object_registry *reg, struct light_object *obj)
{
        //   retry while the exchange FAILS, and stop as soon as it succeeds. Looping on success
        // retries with a now-stale expected value, which decrements twice if another thread
        // restored the count in between; exiting on failure abandons the decrement entirely,
        // which leaks a reference. Under contention the second was measured at ~40% of puts
        uint32_t count = atomic_load(&obj->ref_count);
        do {
                if(count == 0)
                        return;
        } while (!atomic_compare_exchange_weak(&obj->ref_count, &count, count - 1));
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
