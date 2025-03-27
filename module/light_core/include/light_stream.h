#ifndef _LIGHT_STREAM_H
#define _LIGHT_STREAM_H

#ifdef __GNUC__
#define __stream_va_list_type __gnuc_va_list
#endif
// only the 'fast' and 'faster' message types are handled by this facility
#define LIGHT_MSG_FAST                    0
#define LIGHT_MSG_FASTER                  1

// TODO the mqueue interface can probably be excluded from the public API
#define LIGHT_STREAM_MQUEUE_DEPTH             32

struct light_message {
        uint8_t flags;
        const uint8_t *text;
        __stream_va_list_type args;
};

// .message[] is treated as a circular buffer, collisions are avoided by
// checking that .count <= LIGHT_CLI_MQUEUE_DEPTH before writes
struct light_stream_mqueue {
        light_mutex_t lock;
        light_condition_t write_ready;
        uint8_t count;
        uint8_t head;
        struct light_message message[LIGHT_STREAM_MQUEUE_DEPTH];
};

struct light_stream {
        struct light_object obj_header;
        light_mutex_t lock;
        uint8_t mode;
        struct light_stream_mqueue queue;
        int (*handler)(struct light_stream *, const char *restrict);
        int (*handler_va)(struct light_stream *, const char *restrict, ...);
};

extern struct lobj_type ltype_light_stream;

#define to_stream(ptr) container_of(ptr, struct light_stream, obj_header)

#define Light_Stream(_name, _mode, _handler, _handler_va) \
{ \
        .obj_header = Light_Object_RO(_name, NULL, &ltype_light_stream), \
        .mode = _mode, \
        .handler = _handler, \
        .handler_va = _handler_va \
}
#define Light_Stream_Static(_name, _mode, _handler, _handler_va) \
{ \
        .obj_header = Light_Object_Static_RO(_name, NULL, &ltype_light_stream), \
        .mode = _mode, \
        .handler = _handler, \
        .handler_va = _handler_va \
}

#define Light_Stream_Declare(name) \
        extern struct light_stream *name

#define Light_Stream_Define(name, mode, handler, handler_va) \
        struct light_stream __static_buffer _ ## name = Light_Stream_Static(#name, mode, handler, handler_va); \
        struct light_stream __static_stream *name = &_ ## name//; \
//        static const struct light_static_object __static_object autoload_## name = Light_Static_Object(&_ ## name, light__autoload_stream)

Light_Stream_Declare(light_stream_stdout);
Light_Stream_Declare(light_stream_stderr);

// max length of message after substitution is performed
#define LIGHT_STREAM_MAX_MSG_LENGTH        128
#define LIGHT_STREAM_MAX_STREAMS           16

// void light_stream_service_message_queues():
// -> this service function is called automatically by a worker on platforms with threading, or can be
// invoked manually in single-threaded environments
extern void light_stream_service_message_queues();
extern void light_stream_init(struct light_stream *stream);
static inline uint8_t light_stream_get_mode(struct light_stream *stream)
{
        return atomic_load(&stream->mode);
}
static inline const uint8_t *light_stream_get_name(struct light_stream *stream)
{
        return light_object_get_name(&stream->obj_header);
}
static inline struct light_stream_mqueue *light_stream_get_queue(struct light_stream *stream)
{
        return &stream->queue;
}
extern void light_stream_lock_output(struct light_stream *stream);
extern void light_stream_unlock_output(struct light_stream *stream);

extern void light_stream_mqueue_init(struct light_stream_mqueue *queue);
extern void light_stream_mqueue_add_fast(struct light_stream_mqueue *queue, const uint8_t *text);
extern void light_stream_mqueue_add_faster(struct light_stream_mqueue *queue, const uint8_t *text, va_list args);
extern struct light_message *light_stream_mqueue_get(struct light_stream_mqueue *queue);
extern struct light_message *light_stream_mqueue_try_get(struct light_stream_mqueue *queue);
extern bool light_stream_mqueue_is_empty(struct light_stream_mqueue *queue);
extern bool light_stream_mqueue_is_full(struct light_stream_mqueue *queue);

extern uint8_t light_stream_get_background_logging_mode(struct light_stream *stream);
extern void light_stream_set_background_logging_mode(struct light_stream *stream, uint8_t mode);
//   synchronous command-line messages are written to given stream while the caller waits,
// so they should not be sent from signal or interrupt context
extern void light_stream_message_sync(struct light_stream *stream, const uint8_t *message);
extern void light_stream_message_f_sync(struct light_stream *stream, const uint8_t *format, ...);
extern void light_stream_message_vf_sync(struct light_stream *stream, const uint8_t *format, va_list args);
//   'fast' CLI messages are put into a queue that is processed on the main stack,
// but still perform string formatting synchronously before queueing the message
extern void light_stream_message_fast(struct light_stream *stream, const uint8_t *message);
extern void light_stream_message_f_fast(struct light_stream *stream, const uint8_t *format, ...);
extern void light_stream_message_vf_fast(struct light_stream *stream, const uint8_t *format, va_list args);
//   'faster' CLI messages defer all CPU-intensive work for asynchronous processing,
// with the consequence that all referenced objects must remain in scope until
// the kernel worker can perform string formatting
extern void light_stream_message_f_faster(struct light_stream *stream, const uint8_t *format, ...);
extern void light_stream_message_vf_faster(struct light_stream *stream, const uint8_t *format, va_list args);

#endif