#ifndef _LIGHT_OBJECT_H
#define _LIGHT_OBJECT_H

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
        (struct light_object) { \
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


#define Light_Static_Define(_type, sym_name, value) \
        static const _type __static_descriptor _## sym_name = Light_Command_Static(name, parent, description, handler); \
        const struct light_static __static_object *sym_name = &_## name

// initialize default object registry, must be called before any light_object API functions
extern void light_object_setup();
extern struct light_object_registry *light_object_registry_default();

// TODO implement saturation conditions and warnings
extern struct light_object *light_object_get(struct light_object *obj);
extern void light_object_put(struct light_object *obj);

extern void light_object_init(struct light_object *obj, const struct lobj_type *type);

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