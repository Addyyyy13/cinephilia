/*
 * stack.c — pointer stack backed by a doubling array. See dsa/stack.h.
 */
#include "dsa/stack.h"

#include <stdint.h>
#include <stdlib.h>

void stack_init(Stack *s)
{
    s->items = NULL;
    s->count = 0;
    s->cap = 0;
}

void stack_free(Stack *s, void (*free_item)(void *))
{
    if (free_item)
        for (size_t i = 0; i < s->count; i++)
            free_item(s->items[i]);
    free(s->items);
    stack_init(s);
}

bool stack_push(Stack *s, void *item)
{
    if (s->count == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 8;
        if (cap > SIZE_MAX / sizeof(void *))
            return false;
        void **grown = realloc(s->items, cap * sizeof(void *));
        if (!grown)
            return false;
        s->items = grown;
        s->cap = cap;
    }
    s->items[s->count++] = item;
    return true;
}

void *stack_pop(Stack *s)
{
    return s->count ? s->items[--s->count] : NULL;
}

void *stack_peek(const Stack *s)
{
    return s->count ? s->items[s->count - 1] : NULL;
}

bool stack_empty(const Stack *s)
{
    return s->count == 0;
}
