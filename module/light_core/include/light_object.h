#ifndef _LIGHT_OBJECT_H
#define _LIGHT_OBJECT_H

#include <stdarg.h>

#define LIGHT_OBJ_NAME_LENGTH 32

struct light_object {
        light_ref_t ref_count;
        struct light_object *parent;
        const struct lobj_type *type;
        uint16_t state_initialized: 1;
        uint16_t is_static: 1;
        uint16_t is_readonly: 1;
        uint8_t id[LIGHT_OBJ_NAME_LENGTH];
} __packed_aligned;
 
struct lobj_type {
        uint8_t id[LIGHT_OBJ_NAME_LENGTH];
        void (*release)(struct light_object*);
        void (*evt_add)(struct light_object *obj, struct light_object *parent);
        void (*evt_child_add)(struct light_object *obj, struct light_object *child);
        void (*evt_child_remove)(struct light_object *obj, struct light_object *child);
};

struct light_static {
        void **ref;
        void (*load)(void *);
};

#define Light_Object_Full(_id, _parent, _type, _static, _readonly) \
        { \
                .id = _id, \
                .parent = _parent, \
                .type = _type, \
                .is_static = _static, \
                .is_readonly = _readonly \
        }
#define Light_Object(_id, _parent, _type) Light_Object_Full(_id, _parent, _type, 0, 0)
#define Light_Object_Static(_id, _parent, _type) Light_Object_Full(_id, _parent, _type, 1, 0)
#define Light_Object_RO(_id, _parent, _type) Light_Object_Full(_id, _parent, _type, 0, 1)
#define Light_Object_Static_RO(_id, _parent, _type) Light_Object_Full(_id, _parent, _type, 1, 1)

// initialize default object registry, must be called before any light_object API functions
void light_core_impl_setup();
extern struct light_object_registry *light_object_registry_default();

// TODO implement saturation conditions and warnings
extern struct light_object *light_object_get(struct light_object *obj);
extern void light_object_put(struct light_object *obj);

extern void light_object_init(struct light_object *obj, const struct lobj_type *type);

//   an object whose storage the program did not allocate and must never free: a file-scope
// singleton, a module header, an application. Its lifecycle spans the whole execution, so
// light_object_put() will not run its release hook however far the count falls -- release is
// the "free me" hook, and handing static storage to light_free() is a heap corruption rather
// than a leak.
//
//   a thin wrapper rather than a separate per-port primitive: the flag only has to be set
// before anyone else can see the object, and at init time nobody can
static inline void light_object_init_static(struct light_object *obj, const struct lobj_type *type)
{
        light_object_init(obj, type);
        obj->is_static = 1;
}

//   objects declared with the Light_Object_Static* macros carry this from their initialiser;
// ones set up at runtime get it from light_object_init_static(). light_object_init() clears it,
// so a malloc'd object never inherits the bit from whatever was in that memory before
static inline bool light_object_is_static(const struct light_object *obj)
{
        return obj->is_static;
}

static inline const uint8_t *light_object_get_name(struct light_object *obj)
{
        return obj->id;
}

extern void *light_object_alloc(size_t size);
extern void *light_object_alloc_reg(struct light_object_registry *reg, size_t size);
extern void light_object_free(void *obj);
extern void light_object_free_reg(struct light_object_registry *reg, void *obj);

extern int light_object_add(struct light_object *obj, struct light_object *parent,
                                const uint8_t *format, ...);
extern int light_object_add_va(struct light_object *obj, struct light_object *parent,
                                const uint8_t *format, va_list vargs);
extern int light_object_add_reg(struct light_object_registry *reg, struct light_object *obj, struct light_object *parent,
                                const uint8_t *format, ...);
extern int light_object_add_va_reg(struct light_object_registry *reg, struct light_object *obj, struct light_object *parent,
                                const uint8_t *format, va_list vargs);
extern int light_object_del(struct light_object *obj);
extern int light_object_del_reg(struct light_object_registry *reg, struct light_object *obj);

extern struct light_object *light_object_get_reg(struct light_object_registry *reg, struct light_object *obj);
extern void light_object_put_reg(struct light_object_registry *reg, struct light_object *obj);

extern void light_object_init_reg(struct light_object_registry *reg, struct light_object *obj, const struct lobj_type *type);


#endif