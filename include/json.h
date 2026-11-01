/*
 * json.h — a small JSON reader.
 *
 * Parses a JSON document into a tree: every value is a node, and the
 * members of an object or the elements of an array are a linked list of
 * child nodes (first child, next sibling). It is a recursive-descent
 * parser with a depth limit, so hostile input cannot exhaust the stack.
 *
 * Only reading is supported; Cinephilia uses it for TMDB's replies.
 */
#ifndef CINEPHILIA_JSON_H
#define CINEPHILIA_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonType;

typedef struct JsonNode {
    JsonType type;
    char *key;              /* member name when inside an object, else NULL */
    char *string;           /* JSON_STRING: decoded UTF-8 text */
    double number;          /* JSON_NUMBER */
    bool boolean;           /* JSON_BOOL */
    struct JsonNode *child; /* JSON_ARRAY / JSON_OBJECT: first element */
    struct JsonNode *next;  /* next sibling */
} JsonNode;

/* Returns the root node, or NULL if the text is not valid JSON (or memory
 * runs out). Free the result with json_free. */
JsonNode *json_parse(const char *text);
void json_free(JsonNode *node);

/* Member of an object by name, or NULL. */
const JsonNode *json_get(const JsonNode *object, const char *key);
/* Element of an array by position, or NULL. */
const JsonNode *json_at(const JsonNode *array, size_t index);
size_t json_count(const JsonNode *array_or_object);
/* The node's text if it is a string, else `fallback`. */
const char *json_text(const JsonNode *node, const char *fallback);

#endif
