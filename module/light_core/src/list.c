/*
 *  list.c
 *  functions for operating on array-based lists of data
 * 
 *  authored by Alex Fulton
 *  created april 2023
 * 
 */

// FIXME all operations need to have bounds-checking added, for basic safety and security

#include "light_common.h"

#include <stdio.h>
#include <string.h>

int16_t _light_arraylist_indexof(void* (*list)[], uint8_t count, void *item)
{
    for(uint8_t i = 0; i < count; i++) {
        if((*list)[i] == item)
            return i;
    }
    return -1;
}
void _light_arraylist_delete_at_index(void* (*list)[], uint8_t *count, uint8_t index)
{
    for(uint8_t i = index; i < *count - 1; i++) {
        (*list)[i] = (*list)[i + 1];
    }
    (*list)[*count--] = NULL;
}
void _light_arraylist_delete_item(void* (*list)[], uint8_t *count, void *item)
{
    int16_t index;
if((index = _light_arraylist_indexof(list, *count, item)) >= 0)
        light_arraylist_delete_at_index(list, count, index);
}

void _light_arraylist_append(void* (*list)[], uint8_t *count, void *item)
{
        (*list)[(*count)++] = item;
}
void _light_arraylist_insert(void* (*list)[], uint8_t *count, void *item, uint8_t index)
{
        if(index < *count) {
                for(uint8_t i = index; i < *count ; i++) {    
                        (*list)[i + 1] = (*list)[i];
                }
                (*count)++;
        }
        if(index > *count) {
                index = *count;
        }
        (*list)[index] = item;
}
