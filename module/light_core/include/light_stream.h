#ifndef _LIGHT_STREAM_H
#define _LIGHT_STREAM_H

#include <stdarg.h>
#include <stddef.h>

//   for the two sizing macros below, both of which a port may override. Included here rather
// than left to light.h's ordering: light.h pulls in light_stream.h BEFORE light_platform.h,
// so a value defined only in the port header would arrive too late to have any effect. This
// header is nothing but #defines and is guarded, so including it twice costs nothing
#include <light_platform_port.h>

// only the 'fast' and 'faster' message types are handled by this facility
#define LIGHT_MSG_FAST                    0
#define LIGHT_MSG_FASTER                  1

//   a stream is OUTPUT unless declared otherwise: every stream defined before this existed
// (light_stream_stdout/_stderr, and every port's own) is output, so that stays the default.
// INPUT streams are read from rather than written to, and are never touched by the sweep in
// light_stream_service_message_queues() -- see light_stream_read_line()
#define LIGHT_STREAM_OUTPUT                0
#define LIGHT_STREAM_INPUT                 1

// TODO the mqueue interface can probably be excluded from the public API
//
//   HOW TO SIZE THIS, because getting it wrong is a hang rather than a diagnostic. On a
// single-core drain path (LIGHT_PLATFORM_HAS_MULTICORE_WORKER=0) a full queue is a HARD
// DEADLOCK: mqueue_claim_slot() waits on light_condition_wait() for a drain that only the
// main loop can perform, and the main loop is the thing currently blocked filling it.
//   What has to fit is not the whole boot log but the longest run BETWEEN drains.
// light_framework_run() calls light_stream_service_message_queues() at four points, and the
// longest unbroken run is the first: framework init through _load_static_objects(), which is
// where every module-load event and the board's own hardware-init logging lands. Measured,
// that segment is about 10 messages for a minimal application and about 25 for a board that
// brings up a display, touch, IMU, backlight and audio.
//   64 is the default for exactly that reason: 32 covered the base framework boot but not a
// board doing real device creation on top of it. Ports with RAM to spare should leave it
// alone -- the cost is trivial on a part with hundreds of KB, and the failure mode is not.
#ifndef LIGHT_STREAM_MQUEUE_DEPTH
#define LIGHT_STREAM_MQUEUE_DEPTH             64
#endif

//   max length of a fully-formatted message, including the trailing NUL. every message
// is formatted directly into its queue slot at enqueue time (see light_stream_mqueue_add_fast()/
// _add_faster() in stream.c) -- there is no heap allocation anywhere in this pipeline,
// since a queue's total storage is fixed-size and pre-allocated for the life of its
// owning stream, which matters on platforms where the heap is small/fragmentable or
// simply absent.
//   Safer to reduce than the depth is: this one truncates a long line, where the depth
// deadlocks. Total queue storage per stream is DEPTH * (MAX_MSG_LENGTH + 1) bytes, and there
// is one queue per stream
#ifndef LIGHT_STREAM_MAX_MSG_LENGTH
#define LIGHT_STREAM_MAX_MSG_LENGTH        256
#endif

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
        //   _Atomic because it is read with atomic_load() from threads that do not hold the
        // lock. C11 requires the operand of an atomic operation to BE an atomic object -- gcc
        // accepts the plain type and clang rejects it outright, which is the compiler being
        // right: an atomic operation on a non-atomic object is not merely unchecked, it is
        // ill-formed, and nothing guarantees the load is not torn or cached.
        //   plain reads and writes elsewhere stay correct, since accessing an _Atomic object
        // directly is itself a sequentially-consistent atomic operation
        _Atomic uint8_t count;
        uint8_t head;
        struct light_message message[LIGHT_STREAM_MQUEUE_DEPTH];
};

struct light_stream {
        struct light_object obj_header;
        light_mutex_t lock;
        //   _Atomic for the same reason as light_stream_mqueue::count: light_stream_get_mode()
        // and light_stream_set_background_logging_mode() reach it with atomic_load/atomic_store
        // on the C11-threads ports. The comment on light_stream_get_mode() records that the
        // header and the implementation once disagreed about how to touch this field; declaring
        // it atomic is what makes the disagreement impossible rather than merely resolved
        _Atomic uint8_t mode;
        //   LIGHT_STREAM_OUTPUT or LIGHT_STREAM_INPUT. Never atomic: unlike .mode, nothing sets
        // this after the stream's static initialiser runs
        uint8_t direction;
        struct light_stream_mqueue queue;
        int (*handler)(struct light_stream *, const char *restrict);
        int (*handler_va)(struct light_stream *, const char *restrict, ...);
        //   only present on an INPUT stream: attempts to read one line (NUL-terminated, no
        // trailing newline) into 'buffer', returning true if a full line was read and false if
        // none was available. May block the way its own source blocks (e.g. an interactive
        // terminal waiting on a keypress), but must never loop internally -- one call is one
        // attempt, so a caller driven by a periodic task returns control after each
        bool (*read_line)(struct light_stream *, uint8_t *buffer, size_t size);
};

extern struct lobj_type ltype_light_stream;

#define to_stream(ptr) container_of(ptr, struct light_stream, obj_header)

#define Light_Stream(_name, _mode, _handler, _handler_va) \
{ \
        .obj_header = Light_Object_RO(_name, NULL, &ltype_light_stream), \
        .mode = _mode, \
        .direction = LIGHT_STREAM_OUTPUT, \
        .handler = _handler, \
        .handler_va = _handler_va \
}
#define Light_Stream_Static(_name, _mode, _handler, _handler_va) \
{ \
        .obj_header = Light_Object_Static_RO(_name, NULL, &ltype_light_stream), \
        .mode = _mode, \
        .direction = LIGHT_STREAM_OUTPUT, \
        .handler = _handler, \
        .handler_va = _handler_va \
}

#define Light_Stream_Declare(name) \
        extern struct light_stream *name

#define Light_Stream_Define(name, mode, handler, handler_va) \
        struct light_stream __static_buffer _ ## name = Light_Stream_Static(#name, mode, handler, handler_va); \
        struct light_stream __static_stream *name = &_ ## name//; \
//        static const struct light_static_object __static_object autoload_## name = Light_Static_Object(&_ ## name, light__autoload_stream)

//   the input counterpart to Light_Stream_Static()/_Define(): no .mode and no write handlers,
// since nothing ever queues a message to an input stream -- only .read_line is called, and
// only by whoever polls this stream directly (light_cli's console support is the first)
#define Light_Stream_Input_Static(_name, _read_line) \
{ \
        .obj_header = Light_Object_Static_RO(_name, NULL, &ltype_light_stream), \
        .direction = LIGHT_STREAM_INPUT, \
        .read_line = _read_line \
}
#define Light_Stream_Input_Define(name, read_line) \
        struct light_stream __static_buffer _ ## name = Light_Stream_Input_Static(#name, read_line); \
        struct light_stream __static_stream *name = &_ ## name

Light_Stream_Declare(light_stream_stdout);
Light_Stream_Declare(light_stream_stderr);

#define LIGHT_STREAM_MAX_STREAMS           16

// void light_stream_service_message_queues():
// -> this service function is called automatically by a worker on platforms with threading, or can be
// invoked manually in single-threaded environments
extern void light_stream_service_message_queues();
//   blocks until every queued message has been written, or until a timeout says the drain is
// not coming. Called by light_fatal() so that the reason for stopping actually reaches the
// console -- logging is asynchronous, and without this the last message is still queued when
// the process ends. Safe to call whether this core drains the queues or another one does
extern void light_stream_flush();
// how long light_stream_flush() waits on a drain owned by someone else before giving up. Long
// enough for a worker to get through a full queue, short enough that a wedged one does not
// turn an exit into a hang
#ifndef LIGHT_STREAM_FLUSH_TIMEOUT_MS
#define LIGHT_STREAM_FLUSH_TIMEOUT_MS 1000
#endif
//   false until light_stream_setup() has built the queues. Only meaningful where something
// outside light_core drains them -- under LIGHT_PLATFORM_USB_ON_CORE1 the platform's core 1
// USB worker is already running before setup is reached, and must not touch a queue that does
// not exist yet
extern volatile bool light_stream_drain_ready;
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

//   the only way to actually use an INPUT stream: calls its .read_line handler once, returning
// false (without calling it) if 'stream' is not LIGHT_STREAM_INPUT or declares no handler. Never
// touched by light_stream_service_message_queues() -- that sweep is output-only, so an input
// stream is only ever read by something that asks for it directly, on its own schedule
extern bool light_stream_read_line(struct light_stream *stream, uint8_t *buffer, size_t size);

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
