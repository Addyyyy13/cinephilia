/*
 * stack.h — a growable LIFO stack of pointers.
 *
 * Used for "undo delete": each deletion pushes a record holding the removed
 * film and its journal entries; undo pops the most recent one. Last in,
 * first out is exactly the order a user expects undo to follow.
 */
#ifndef CINEPHILIA_DSA_STACK_H
#define CINEPHILIA_DSA_STACK_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void **items;
    size_t count;
    size_t cap;
} Stack;

void stack_init(Stack *s);
/* Frees the stack; when free_item is non-NULL it is called on each item. */
void stack_free(Stack *s, void (*free_item)(void *));
bool stack_push(Stack *s, void *item); /* false on allocation failure */
void *stack_pop(Stack *s);             /* NULL when empty */
void *stack_peek(const Stack *s);      /* NULL when empty */
bool stack_empty(const Stack *s);

#endif
