/*
 *  list.c
 *  functions for operating on array-based lists of data
 * 
 *  authored by Alex Fulton
 *  created april 2023
 * 
 */

// FIXME all operations need to have bounds-checking added, for basic safety and security

#include <light.h>

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
        //   an index at or past the end has nothing to delete, and without this the loop below
        // runs against `*count - 1` on an empty list, which underflows to 255
        if(index >= *count)
                return;

        for(uint8_t i = index; i < *count - 1; i++) {
                (*list)[i] = (*list)[i + 1];
        }

        //   (*count)--, NOT *count--. The latter parses as *(count--): it decrements the
        // POINTER -- a local copy, discarded on return -- so the caller's count was never
        // reduced, and every caller iterating `i < count` went on seeing a duplicate of the
        // last element. It also indexed with the ORIGINAL count, writing NULL one past the
        // last element, which on a full array is outside it entirely.
        (*count)--;
        (*list)[*count] = NULL;
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
        //   clamped first, so the three cases -- inside the list, exactly at the end, past the
        // end -- all become one. The previous version handled them in separate branches and
        // incremented the count in only ONE of them, so inserting at or beyond the end stored
        // the item without the list ever growing to include it.
        if(index > *count)
                index = *count;

        //   DESCENDING. Copying (*list)[i] to (*list)[i + 1] with i ascending overwrites each
        // element before it has been read, so the element at `index` was propagated across
        // every slot after it and the rest of the tail was lost. Walking down copies each one
        // into the space just vacated above it.
        for(uint8_t i = *count; i > index; i--) {
                (*list)[i] = (*list)[i - 1];
        }

        (*list)[index] = item;
        (*count)++;
}
