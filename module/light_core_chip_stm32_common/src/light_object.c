/*
 *  light_object.c
 *  core definitions for the light object model, STM32 (bare CMSIS)
 *
 *  authored by Alex Fulton
 *  created august 2026
 *
 */
#include <light.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

//   PRIMASK is saved into the mutex and restored on unlock, rather than unconditionally
// re-enabling interrupts. that is what makes nesting safe: a lock taken while already inside a
// critical region restores "still masked" instead of opening an interrupt window the outer
// region was relying on being closed.
//   __disable_irq() after reading PRIMASK, not before: reading it first and masking second
// leaves a window in which an interrupt could take a lock of its own, and the value we saved
// would then describe the wrong state.
void light_core_port_mutex_lock(light_mutex_t *mutex)
{
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        *mutex = primask;
}
void light_core_port_mutex_unlock(light_mutex_t *mutex)
{
        uint32_t primask = *mutex;
        // only re-enable if this lock is the one that masked them
        if(!primask)
                __enable_irq();
}

static bool _registry_loaded = false;
static struct light_object_registry _registry_default;

//   every access to a reference count or to a parent link goes through these. A read-modify-
// write is not atomic on this core, and an interrupt landing in the middle of one is all it
// takes -- there is no second core here, but there is no shortage of handlers.
//
//   NOT RE-ENTRANT, and the whole file is shaped around that: light_mutex_do_lock() saves
// PRIMASK into the single word the matching unlock restores from, so a nested lock overwrites
// the outer one's saved state with "already masked" and the outer unlock then declines to
// re-enable interrupts at all -- they stay off for good. Nothing between a lock and its unlock
// may call a light_object_* entry point, and no user callback may be fired there
static void _registry_critical_enter(struct light_object_registry *reg)
{
        light_mutex_do_lock(&reg->mutex);
}
static void _registry_critical_exit(struct light_object_registry *reg)
{
        light_mutex_do_unlock(&reg->mutex);
}

//   the reference-count halves of get() and put(), to be called with the registry lock ALREADY
// HELD. They exist so an operation that has to establish a link and its reference together --
// add() and del() below -- can do both inside one section, which calling light_object_get_reg()
// from in there could not do without wedging interrupts off per the note above
static struct light_object *_get_locked(struct light_object *obj)
{
        if(!obj || obj->ref_count == 0)
                return NULL;
        obj->ref_count++;
        return obj;
}
//   returns true if THIS call took the count to zero, which is how the caller knows it is the
// one that has to run the release hook. Exactly one caller can ever see that, however many are
// racing, which is what makes the hook fire exactly once without a second flag to get wrong
static bool _put_locked(struct light_object *obj)
{
        // the NULL test is not redundant: light_object_del_reg() puts a parent that a
        // concurrent del may already have detached
        if(!obj || obj->ref_count == 0)
                return false;
        return --obj->ref_count == 0;
}

void light_core_impl_setup()
{
        if(!_registry_loaded) {
                _registry_loaded = true;
                light_mutex_init(&_registry_default.mutex);
                _registry_default.alloc = light_alloc;
                _registry_default.free = light_free;
        }
}
//   no scheduler to block on, so this spin-polls -- see light_condition_t in
// light_core_port.h. must be called with 'mutex' already held, per the light_condition_wait()
// contract used throughout light_core/src/stream.c.
//   __WFI() rather than a bare spin: with LIGHT_PLATFORM_HAS_MULTICORE_WORKER = 0 the only
// thing that can ever set this flag is an interrupt handler, so sleeping until one arrives is
// both correct and the difference between idling at a few mA and idling at full core current.
// the mutex is released across the wait, which is what allows that handler to run at all
void light_core_port_condition_wait(light_condition_t *cond, light_mutex_t *mutex)
{
        light_mutex_do_unlock(mutex);
        while(!*cond) {
                __WFI();
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
//   callbacks only, and deliberately called with the lock RELEASED. Firing them inside the
// section would wedge interrupts off the moment a callback touched any light_object_* function,
// and device libraries hang their initialisation off evt_add, so that is a live path rather
// than a hypothetical. It would also hold interrupts off across arbitrary user code.
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
        // interrupt handler can observe the object attached but not yet holding its reference
        // -- the window in which a concurrent del() would detach it and drop a reference that
        // had not been taken. Note a dead parent leaves the link set but yields no reference,
        // which is the behaviour this has always had
        _registry_critical_enter(reg);
        obj->parent = parent;
        parent = _get_locked(parent);
        _registry_critical_exit(reg);

        return light_object_add_internal(reg, obj, parent);
}
int light_object_add(struct light_object *obj, struct light_object *parent,
                            const uint8_t *format, ...)
{
        va_list vargs;
        int retval;

        va_start(vargs, format);
        retval = light_object_add_va(obj, parent, format, vargs);
        va_end(vargs);

        return retval;
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

        //   read the link and clear it in one section, so two paths detaching the same object
        // cannot both come away with the same parent and release it twice. Whoever loses sees
        // NULL and does nothing
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

//   no lock, and that is not an oversight: init runs before the object is reachable by anyone
// else, so there is no second party to exclude. Taking the section here would order these two
// stores against nothing at all
void light_object_init_reg(struct light_object_registry *reg, struct light_object *obj, const struct lobj_type *type)
{
        obj->ref_count = 1;
        obj->type = type;
        //   cleared explicitly, not left as found. light_object_alloc() hands back malloc'd
        // memory, so these bits would otherwise be whatever the previous occupant left there --
        // and an is_static that came up 1 by chance would silently suppress the release hook
        // and leak the object. light_object_init_static() sets the bit back afterwards
        obj->is_static = 0;
        obj->is_readonly = 0;
        obj->state_initialized = 1;
}
// TODO implement saturation conditions and warnings
struct light_object *light_object_get_reg(struct light_object_registry *reg, struct light_object *obj)
{
        struct light_object *ref;

        _registry_critical_enter(reg);
        ref = _get_locked(obj);
        _registry_critical_exit(reg);

        return ref;
}
void light_object_put_reg(struct light_object_registry *reg, struct light_object *obj)
{
        bool died;

        //   _put_locked() refuses to decrement past zero: on an unsigned count 0 - 1 is not a
        // small negative number but ~4 billion, which leaves a released object looking
        // permanently alive and impossible to release again. The section makes the
        // read-modify-write atomic; it says nothing about whether the value should be
        // decremented at all
        _registry_critical_enter(reg);
        died = _put_locked(obj);
        _registry_critical_exit(reg);

        //   the release hook runs OUTSIDE the section, for the same reasons the add and del
        // callbacks do: it is user code, it typically frees the object, and the lock is not
        // re-entrant, so a destructor that put()s a child would wedge interrupts off. obj must
        // not be touched after this returns -- the hook has very likely freed it
        //   a static object is never released: its storage outlives the program's interest in
        // it, and release is the hook that frees. Reaching zero on one is not an error -- it
        // just means nothing holds a reference any more, which for file-scope storage is fine
        if(died && !light_object_is_static(obj) && obj->type->release)
                obj->type->release(obj);
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
        return LIGHT_OK;
}
void light_ref_put(light_ref_t *ref)
{
}
