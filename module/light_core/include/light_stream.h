#ifndef _LIGHT_STREAM_H
#define _LIGHT_STREAM_H

#include <stdarg.h>

// only the 'fast' and 'faster' message types are handled by this facility
#define LIGHT_MSG_FAST                    0
#define LIGHT_MSG_FASTER                  1

// TODO the mqueue interface can probably be excluded from the public API
// on a single-core drain path (see light_stream_setup()'s LIGHT_PLATFORM_HAS_MULTICORE_WORKER=0
// branch), filling this queue during synchronous boot is a hard deadlock -- nothing else
// can run to drain it. 32 was enough for the base framework boot sequence alone, but not
// once a board's hardware-init callback does enough of its own logging during device
// creation on top of that -- bumped for headroom; cost is trivial (32 extra 257-byte
// slots per stream) on every platform this runs on
#define LIGHT_STREAM_MQUEUE_DEPTH             64

// max length of a fully-formatted message, including the trailing NUL. every message
// is formatted directly into its queue slot at enqueue time (see light_stream_mqueue_add_fast()/
// _add_faster() in stream.c) -- there is no heap allocation anywhere in this pipeline,
// since a queue's total storage is fixed-size and pre-allocated for the life of its
// owning stream, which matters on platforms where the heap is small/fragmentable or
// simply absent
#define LIGHT_STREAM_MAX_MSG_LENGTH        256

struct light_message {
        uint8_t flags;
        uint8_t text[LIGHT_STREAM_MAX_MSG_LENGTH];
};

// .message[] is treated as a circular buffer, collisions are avoided by
// checking that .count <= LIGHT_CLI_MQUEUE_DEPTH before writes
//
// this struct is always embedded in a struct light_stream and shares that stream's own
// light_mutex_t rather than owning one of its own (see the light_stream_mqueue_* signatures
// below, which take the owning stream's lock explicitly) -- on platforms where each mutex
// permanently consumes a scarce shared resource (e.g. RP2040's 8 general-purpose hardware
// spinlocks), a stream's output lock and its queue lock are never held at the same time, so
// giving each stream two separate locks wastes half of that budget for no benefit
struct light_stream_mqueue {
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

#define LIGHT_STREAM_MAX_STREAMS           16

// void light_stream_service_message_queues():
// -> this service function is called automatically by a worker on platforms with threading, or can be
// invoked manually in single-threaded environments
extern void light_stream_service_message_queues();
extern void light_stream_init(struct light_stream *stream);
static inline uint8_t light_stream_get_mode(struct light_stream *stream)
{
// #if, not #ifdef. Every port DEFINES this macro -- to 0 where the feature is absent -- so
// #ifdef was true everywhere, and this header took the atomic branch on the embedded ports
// while light_stream_set_background_logging_mode() in stream.c, which tests it with #if, took
// the plain one. The header and the implementation disagreed about the same field
#if LIGHT_PLATFORM_HAS_C11_THREADS
        return atomic_load(&stream->mode);
#else
        return stream->mode;
#endif
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
extern void light_stream_mqueue_add_fast(light_mutex_t *lock, struct light_stream_mqueue *queue, const uint8_t *text);
extern void light_stream_mqueue_add_faster(light_mutex_t *lock, struct light_stream_mqueue *queue, const uint8_t *format, va_list args);
// copies the oldest queued message into *out (by value; no heap involved) and returns
// true, or returns false without touching *out if the queue was empty. on a tight
// stack budget, consider light_stream_mqueue_peek()/_advance() instead, which process
// the message in place rather than copying the whole (~256-byte) struct onto the stack
extern bool light_stream_mqueue_get(light_mutex_t *lock, struct light_stream_mqueue *queue, struct light_message *out);
extern bool light_stream_mqueue_try_get(light_mutex_t *lock, struct light_stream_mqueue *queue, struct light_message *out);
// lower-level pair for processing a message in place under lock, without copying it
// onto the caller's stack: peek() returns a pointer to the oldest queued message
// (queue must not be empty), the caller uses it directly, then advance() removes it.
// caller must hold 'lock' across both calls, and not call peek() on an empty queue
extern struct light_message *light_stream_mqueue_peek(struct light_stream_mqueue *queue);
extern void light_stream_mqueue_advance(struct light_stream_mqueue *queue);
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
//   'faster' CLI messages skip the log-level/function-name prefix 'fast' messages get
// added by light_log_internal(), but are otherwise formatted immediately, the same as
// 'fast' -- deferring formatting to the consumer was the original design here, but it
// required storing a va_list in the queue past the return of the stack frame that
// started it, which is undefined behaviour
extern void light_stream_message_f_faster(struct light_stream *stream, const uint8_t *format, ...);
extern void light_stream_message_vf_faster(struct light_stream *stream, const uint8_t *format, va_list args);

#endif