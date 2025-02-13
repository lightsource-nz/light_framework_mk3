/*
 *  light_cli/src/message.c
 *  functions implementing the light_cli console message API
 * 
 *  authored by Alex Fulton
 *  created november 2024
 * 
 */
#include <light.h>
#include <light_cli.h>
#include <stdio.h>

#include "cli_private.h"

#define LTYPE_CLI_MSG_QUEUE_NAME        "cli_message_queue"
static void _cli_message_queue_release(struct light_object *obj);
static void _cli_message_queue_event_add(struct light_object *obj, struct light_object *child);
struct lobj_type ltype_cli_message_queue = {
        .id = LTYPE_CLI_MSG_QUEUE_NAME,
        .release = _cli_message_queue_release,
        .evt_add = _cli_message_queue_event_add,
        .evt_child_add = NULL,
        .evt_child_remove = NULL
};
Light_CLI_MQueue_Define(light_cli_mqueue_default);

static void _cli_message_queue_release(struct light_object *obj)
{
        light_free(to_mqueue(obj));
}
static void _cli_message_queue_event_add(struct light_object *obj, struct light_object *child)
{

}

void light_cli_message_init()
{
        light_cli_mqueue_init(&light_cli_mqueue_default);
}
void light_cli_mqueue_init(struct light_cli_mqueue *queue)
{
        light_object_init(&queue->obj_header, &ltype_cli_message_queue);
        queue->count = 0;
        queue->head = 0;
}
// TODO make this structure thread-safe
void light_cli_mqueue_add(struct light_cli_mqueue *queue, uint8_t flags, uint8_t *text, uint8_t argc, void *argv)
{
        uint8_t index = queue->head;
        if(queue->count < LIGHT_CLI_MQUEUE_DEPTH) {
                queue->count++;
                queue->head = (queue->head + 1) % LIGHT_CLI_MQUEUE_DEPTH;
                queue->message[index] = (struct light_cli_message) {
                        .flags = flags,
                        .text = text,
                        .argc = argc,
                        .argv = argv
                };
        }
}
void light_cli_message_sync(const uint8_t *message)
{
        printf(message);
}
extern void light_cli_message_f_sync(const uint8_t *format, ...)
{
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
}
extern void light_cli_message_fast(const uint8_t *message)
{

}
extern void light_cli_message_f_fast(const uint8_t *format, ...);
extern void light_cli_message_f_faster(const uint8_t *format, ...);
