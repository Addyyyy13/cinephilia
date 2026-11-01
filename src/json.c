/*
 * json.c — recursive-descent JSON parser. See json.h.
 */
#include "json.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 64

typedef struct {
    const char *p;
    int depth;
} Parser;

static JsonNode *parse_value(Parser *ps);

static void skip_space(Parser *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')
        ps->p++;
}

static JsonNode *new_node(JsonType type)
{
    JsonNode *n = calloc(1, sizeof *n);
    if (n)
        n->type = type;
    return n;
}

void json_free(JsonNode *node)
{
    while (node) {
        JsonNode *next = node->next;
        json_free(node->child);
        free(node->key);
        free(node->string);
        free(node);
        node = next;
    }
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Reads four hex digits; returns -1 if they are not hex. */
static long read_hex4(const char *s)
{
    long v = 0;
    for (int i = 0; i < 4; i++) {
        int h = hex_value(s[i]);
        if (h < 0)
            return -1;
        v = v * 16 + h;
    }
    return v;
}

static size_t utf8_encode(unsigned long cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Parses a string starting at the opening quote. Returns a new buffer with
 * escapes decoded, or NULL on error. The decoded text is never longer than
 * the source, so one allocation of the source length is enough. */
static char *parse_string(Parser *ps)
{
    if (*ps->p != '"')
        return NULL;
    const char *start = ++ps->p;
    const char *end = start;
    while (*end && *end != '"') {
        if (*end == '\\' && end[1])
            end++;
        end++;
    }
    if (*end != '"')
        return NULL;
    char *out = malloc((size_t)(end - start) + 1);
    if (!out)
        return NULL;
    size_t o = 0;
    const char *s = start;
    while (s < end) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20)
            goto fail; /* raw control characters are not allowed */
        if (c != '\\') {
            out[o++] = (char)c;
            s++;
            continue;
        }
        s++;
        switch (*s) {
        case '"': out[o++] = '"'; s++; break;
        case '\\': out[o++] = '\\'; s++; break;
        case '/': out[o++] = '/'; s++; break;
        case 'b': out[o++] = '\b'; s++; break;
        case 'f': out[o++] = '\f'; s++; break;
        case 'n': out[o++] = '\n'; s++; break;
        case 'r': out[o++] = '\r'; s++; break;
        case 't': out[o++] = '\t'; s++; break;
        case 'u': {
            if (end - s < 5)
                goto fail;
            long cp = read_hex4(s + 1);
            if (cp < 0)
                goto fail;
            s += 5;
            if (cp >= 0xD800 && cp <= 0xDBFF) { /* surrogate pair */
                long lo = (end - s >= 6 && s[0] == '\\' && s[1] == 'u') ? read_hex4(s + 2) : -1;
                if (lo < 0xDC00 || lo > 0xDFFF)
                    goto fail;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                s += 6;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                goto fail;
            }
            o += utf8_encode((unsigned long)cp, out + o);
            break;
        }
        default:
            goto fail;
        }
    }
    out[o] = '\0';
    ps->p = end + 1;
    return out;
fail:
    free(out);
    return NULL;
}

static JsonNode *parse_number(Parser *ps)
{
    const char *s = ps->p;
    if (*s == '-')
        s++;
    if (*s < '0' || *s > '9')
        return NULL;
    char *end = NULL;
    double v = strtod(ps->p, &end);
    if (end == ps->p)
        return NULL;
    JsonNode *n = new_node(JSON_NUMBER);
    if (!n)
        return NULL;
    n->number = v;
    ps->p = end;
    return n;
}

static bool match(Parser *ps, const char *word)
{
    size_t n = strlen(word);
    if (strncmp(ps->p, word, n) != 0)
        return false;
    ps->p += n;
    return true;
}

/* Parses the items of an array or object; `close` is ']' or '}'. */
static JsonNode *parse_container(Parser *ps, JsonType type, char close)
{
    if (++ps->depth > MAX_DEPTH)
        return NULL;
    JsonNode *node = new_node(type);
    if (!node)
        return NULL;
    ps->p++; /* the opening bracket */
    skip_space(ps);
    JsonNode **tail = &node->child;
    if (*ps->p == close) {
        ps->p++;
        ps->depth--;
        return node;
    }
    for (;;) {
        char *key = NULL;
        skip_space(ps);
        if (type == JSON_OBJECT) {
            key = parse_string(ps);
            if (!key)
                goto fail;
            skip_space(ps);
            if (*ps->p != ':') {
                free(key);
                goto fail;
            }
            ps->p++;
        }
        JsonNode *item = parse_value(ps);
        if (!item) {
            free(key);
            goto fail;
        }
        item->key = key;
        *tail = item; /* append, keeping document order */
        tail = &item->next;
        skip_space(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == close) {
            ps->p++;
            ps->depth--;
            return node;
        }
        goto fail;
    }
fail:
    json_free(node);
    return NULL;
}

static JsonNode *parse_value(Parser *ps)
{
    skip_space(ps);
    switch (*ps->p) {
    case '{':
        return parse_container(ps, JSON_OBJECT, '}');
    case '[':
        return parse_container(ps, JSON_ARRAY, ']');
    case '"': {
        char *s = parse_string(ps);
        if (!s)
            return NULL;
        JsonNode *n = new_node(JSON_STRING);
        if (!n) {
            free(s);
            return NULL;
        }
        n->string = s;
        return n;
    }
    case 't':
    case 'f': {
        bool value = *ps->p == 't';
        if (!match(ps, value ? "true" : "false"))
            return NULL;
        JsonNode *n = new_node(JSON_BOOL);
        if (n)
            n->boolean = value;
        return n;
    }
    case 'n':
        return match(ps, "null") ? new_node(JSON_NULL) : NULL;
    default:
        return parse_number(ps);
    }
}

JsonNode *json_parse(const char *text)
{
    if (!text)
        return NULL;
    Parser ps = {text, 0};
    JsonNode *root = parse_value(&ps);
    if (!root)
        return NULL;
    skip_space(&ps);
    if (*ps.p != '\0') { /* trailing garbage */
        json_free(root);
        return NULL;
    }
    return root;
}

const JsonNode *json_get(const JsonNode *object, const char *key)
{
    if (!object || object->type != JSON_OBJECT)
        return NULL;
    for (const JsonNode *c = object->child; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0)
            return c;
    return NULL;
}

const JsonNode *json_at(const JsonNode *array, size_t index)
{
    if (!array || array->type != JSON_ARRAY)
        return NULL;
    const JsonNode *c = array->child;
    while (c && index > 0) {
        c = c->next;
        index--;
    }
    return c;
}

size_t json_count(const JsonNode *node)
{
    size_t n = 0;
    if (node && (node->type == JSON_ARRAY || node->type == JSON_OBJECT))
        for (const JsonNode *c = node->child; c; c = c->next)
            n++;
    return n;
}

const char *json_text(const JsonNode *node, const char *fallback)
{
    return (node && node->type == JSON_STRING) ? node->string : fallback;
}
