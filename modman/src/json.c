#define _POSIX_C_SOURCE 200809L

#include "json.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *begin;
    const char *p;
    const char *end;
    char *error;
} parser;

static void set_error(parser *p, const char *message)
{
    if (!p->error) {
        p->error = strdup(message);
    }
}

static void skip_space(parser *p)
{
    while (p->p < p->end && (*p->p == ' ' || *p->p == '\t' ||
                             *p->p == '\r' || *p->p == '\n')) {
        p->p++;
    }
}

static json_value *new_value(json_type type)
{
    json_value *value = calloc(1, sizeof(*value));
    if (value) {
        value->type = type;
    }
    return value;
}

static int append_utf8(char **out, size_t *len, size_t *cap, uint32_t cp)
{
    uint8_t bytes[4];
    size_t count;
    if (cp <= 0x7f) {
        bytes[0] = (uint8_t)cp;
        count = 1;
    } else if (cp <= 0x7ff) {
        bytes[0] = (uint8_t)(0xc0 | (cp >> 6));
        bytes[1] = (uint8_t)(0x80 | (cp & 0x3f));
        count = 2;
    } else if (cp <= 0xffff) {
        bytes[0] = (uint8_t)(0xe0 | (cp >> 12));
        bytes[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
        bytes[2] = (uint8_t)(0x80 | (cp & 0x3f));
        count = 3;
    } else if (cp <= 0x10ffff) {
        bytes[0] = (uint8_t)(0xf0 | (cp >> 18));
        bytes[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3f));
        bytes[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
        bytes[3] = (uint8_t)(0x80 | (cp & 0x3f));
        count = 4;
    } else {
        return -1;
    }
    if (*len + count + 1 > *cap) {
        size_t next = *cap ? *cap * 2 : 32;
        while (next < *len + count + 1) {
            next *= 2;
        }
        char *tmp = realloc(*out, next);
        if (!tmp) {
            return -1;
        }
        *out = tmp;
        *cap = next;
    }
    memcpy(*out + *len, bytes, count);
    *len += count;
    return 0;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *parse_string(parser *p)
{
    if (p->p >= p->end || *p->p != '"') {
        set_error(p, "expected string");
        return NULL;
    }
    p->p++;
    char *out = NULL;
    size_t len = 0;
    size_t cap = 0;
    while (p->p < p->end) {
        unsigned char c = (unsigned char)*p->p++;
        if (c == '"') {
            if (len + 1 > cap) {
                char *tmp = realloc(out, len + 1);
                if (!tmp) {
                    set_error(p, "out of memory");
                    free(out);
                    return NULL;
                }
                out = tmp;
            }
            out[len] = '\0';
            return out;
        }
        if (c < 0x20) {
            set_error(p, "control character in string");
            free(out);
            return NULL;
        }
        if (c == '\\') {
            if (p->p >= p->end) {
                set_error(p, "truncated escape");
                free(out);
                return NULL;
            }
            char esc = *p->p++;
            uint32_t cp;
            switch (esc) {
            case '"': cp = '"'; break;
            case '\\': cp = '\\'; break;
            case '/': cp = '/'; break;
            case 'b': cp = '\b'; break;
            case 'f': cp = '\f'; break;
            case 'n': cp = '\n'; break;
            case 'r': cp = '\r'; break;
            case 't': cp = '\t'; break;
            case 'u':
                if (p->end - p->p < 4) {
                    set_error(p, "truncated unicode escape");
                    free(out);
                    return NULL;
                }
                cp = 0;
                for (int i = 0; i < 4; i++) {
                    int v = hex_value(p->p[i]);
                    if (v < 0) {
                        set_error(p, "invalid unicode escape");
                        free(out);
                        return NULL;
                    }
                    cp = (cp << 4) | (uint32_t)v;
                }
                p->p += 4;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (p->end - p->p < 6 || p->p[0] != '\\' ||
                        p->p[1] != 'u') {
                        set_error(p, "unpaired surrogate");
                        free(out);
                        return NULL;
                    }
                    p->p += 2;
                    uint32_t low = 0;
                    for (int i = 0; i < 4; i++) {
                        int v = hex_value(p->p[i]);
                        if (v < 0) {
                            set_error(p, "invalid surrogate");
                            free(out);
                            return NULL;
                        }
                        low = (low << 4) | (uint32_t)v;
                    }
                    p->p += 4;
                    if (low < 0xdc00 || low > 0xdfff) {
                        set_error(p, "invalid surrogate pair");
                        free(out);
                        return NULL;
                    }
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                    set_error(p, "unpaired surrogate");
                    free(out);
                    return NULL;
                }
                break;
            default:
                set_error(p, "invalid escape");
                free(out);
                return NULL;
            }
            if (append_utf8(&out, &len, &cap, cp) != 0) {
                set_error(p, "out of memory");
                free(out);
                return NULL;
            }
            continue;
        }
        if (len + 2 > cap) {
            size_t next = cap ? cap * 2 : 32;
            char *tmp = realloc(out, next);
            if (!tmp) {
                set_error(p, "out of memory");
                free(out);
                return NULL;
            }
            out = tmp;
            cap = next;
        }
        out[len++] = (char)c;
    }
    set_error(p, "unterminated string");
    free(out);
    return NULL;
}

static json_value *parse_value(parser *p, int depth);

static json_value *parse_array(parser *p, int depth)
{
    json_value *value = new_value(JSON_ARRAY);
    if (!value) {
        set_error(p, "out of memory");
        return NULL;
    }
    p->p++;
    skip_space(p);
    if (p->p < p->end && *p->p == ']') {
        p->p++;
        return value;
    }
    for (;;) {
        json_value *item = parse_value(p, depth + 1);
        if (!item) {
            json_free(value);
            return NULL;
        }
        json_value **items = realloc(value->u.array.items,
                                     (value->u.array.count + 1) *
                                     sizeof(*items));
        if (!items) {
            json_free(item);
            json_free(value);
            set_error(p, "out of memory");
            return NULL;
        }
        value->u.array.items = items;
        value->u.array.items[value->u.array.count++] = item;
        skip_space(p);
        if (p->p >= p->end) {
            json_free(value);
            set_error(p, "unterminated array");
            return NULL;
        }
        if (*p->p == ']') {
            p->p++;
            return value;
        }
        if (*p->p != ',') {
            json_free(value);
            set_error(p, "expected comma in array");
            return NULL;
        }
        p->p++;
        skip_space(p);
    }
}

static json_value *parse_object(parser *p, int depth)
{
    json_value *value = new_value(JSON_OBJECT);
    if (!value) {
        set_error(p, "out of memory");
        return NULL;
    }
    p->p++;
    skip_space(p);
    if (p->p < p->end && *p->p == '}') {
        p->p++;
        return value;
    }
    for (;;) {
        skip_space(p);
        char *key = parse_string(p);
        if (!key) {
            json_free(value);
            return NULL;
        }
        skip_space(p);
        if (p->p >= p->end || *p->p != ':') {
            free(key);
            json_free(value);
            set_error(p, "expected colon");
            return NULL;
        }
        p->p++;
        json_value *item = parse_value(p, depth + 1);
        if (!item) {
            free(key);
            json_free(value);
            return NULL;
        }
        for (size_t i = 0; i < value->u.object.count; i++) {
            if (strcmp(value->u.object.keys[i], key) == 0) {
                free(key);
                json_free(item);
                json_free(value);
                set_error(p, "duplicate object key");
                return NULL;
            }
        }
        char **keys = realloc(value->u.object.keys,
                              (value->u.object.count + 1) * sizeof(*keys));
        if (!keys) {
            free(key);
            json_free(item);
            json_free(value);
            set_error(p, "out of memory");
            return NULL;
        }
        value->u.object.keys = keys;
        json_value **values = realloc(value->u.object.values,
                                      (value->u.object.count + 1) *
                                      sizeof(*values));
        if (!values) {
            free(key);
            json_free(item);
            json_free(value);
            set_error(p, "out of memory");
            return NULL;
        }
        value->u.object.values = values;
        value->u.object.keys[value->u.object.count] = key;
        value->u.object.values[value->u.object.count] = item;
        value->u.object.count++;
        skip_space(p);
        if (p->p >= p->end) {
            json_free(value);
            set_error(p, "unterminated object");
            return NULL;
        }
        if (*p->p == '}') {
            p->p++;
            return value;
        }
        if (*p->p != ',') {
            json_free(value);
            set_error(p, "expected comma in object");
            return NULL;
        }
        p->p++;
    }
}

static json_value *parse_number(parser *p)
{
    const char *start = p->p;
    if (p->p < p->end && *p->p == '-') {
        p->p++;
    }
    if (p->p >= p->end || !isdigit((unsigned char)*p->p)) {
        set_error(p, "invalid number");
        return NULL;
    }
    if (*p->p == '0' && p->p + 1 < p->end &&
        isdigit((unsigned char)p->p[1])) {
        set_error(p, "leading zero in number");
        return NULL;
    }
    while (p->p < p->end && isdigit((unsigned char)*p->p)) {
        p->p++;
    }
    if (p->p < p->end && (*p->p == '.' || *p->p == 'e' || *p->p == 'E')) {
        set_error(p, "only integer numbers are supported");
        return NULL;
    }
    errno = 0;
    char buffer[64];
    size_t len = (size_t)(p->p - start);
    if (len >= sizeof(buffer)) {
        set_error(p, "number too long");
        return NULL;
    }
    memcpy(buffer, start, len);
    buffer[len] = '\0';
    long long number = strtoll(buffer, NULL, 10);
    if (errno != 0) {
        set_error(p, "number out of range");
        return NULL;
    }
    json_value *value = new_value(JSON_NUMBER);
    if (!value) {
        set_error(p, "out of memory");
        return NULL;
    }
    value->u.number = number;
    return value;
}

static json_value *parse_value(parser *p, int depth)
{
    if (depth > 32) {
        set_error(p, "nesting too deep");
        return NULL;
    }
    skip_space(p);
    if (p->p >= p->end) {
        set_error(p, "unexpected end of input");
        return NULL;
    }
    if (*p->p == '{') return parse_object(p, depth);
    if (*p->p == '[') return parse_array(p, depth);
    if (*p->p == '"') {
        char *string = parse_string(p);
        if (!string) return NULL;
        json_value *value = new_value(JSON_STRING);
        if (!value) {
            free(string);
            set_error(p, "out of memory");
            return NULL;
        }
        value->u.string = string;
        return value;
    }
    if (p->end - p->p >= 4 && strncmp(p->p, "true", 4) == 0) {
        p->p += 4;
        json_value *value = new_value(JSON_BOOL);
        if (!value) {
            set_error(p, "out of memory");
            return NULL;
        }
        value->u.boolean = 1;
        return value;
    }
    if (p->end - p->p >= 5 && strncmp(p->p, "false", 5) == 0) {
        p->p += 5;
        json_value *value = new_value(JSON_BOOL);
        if (!value) {
            set_error(p, "out of memory");
            return NULL;
        }
        return value;
    }
    if (p->end - p->p >= 4 && strncmp(p->p, "null", 4) == 0) {
        p->p += 4;
        return new_value(JSON_NULL);
    }
    if (*p->p == '-' || isdigit((unsigned char)*p->p)) {
        return parse_number(p);
    }
    set_error(p, "invalid value");
    return NULL;
}

json_value *json_parse(const char *text, char **error)
{
    parser p = {
        .begin = text,
        .p = text,
        .end = text + strlen(text),
        .error = NULL
    };
    json_value *value = parse_value(&p, 0);
    if (!value) {
        if (error) {
            *error = p.error ? p.error : strdup("invalid JSON");
        } else {
            free(p.error);
        }
        return NULL;
    }
    skip_space(&p);
    if (p.p != p.end) {
        json_free(value);
        if (error) {
            *error = strdup("trailing data after JSON value");
        }
        return NULL;
    }
    if (error) {
        *error = NULL;
    }
    return value;
}

void json_free(json_value *value)
{
    if (!value) return;
    switch (value->type) {
    case JSON_STRING:
        free(value->u.string);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < value->u.array.count; i++) {
            json_free(value->u.array.items[i]);
        }
        free(value->u.array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < value->u.object.count; i++) {
            free(value->u.object.keys[i]);
            json_free(value->u.object.values[i]);
        }
        free(value->u.object.keys);
        free(value->u.object.values);
        break;
    default:
        break;
    }
    free(value);
}

const json_value *json_get(const json_value *object, const char *key)
{
    if (!object || object->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < object->u.object.count; i++) {
        if (strcmp(object->u.object.keys[i], key) == 0) {
            return object->u.object.values[i];
        }
    }
    return NULL;
}

const char *json_string(const json_value *value)
{
    return value && value->type == JSON_STRING ? value->u.string : NULL;
}

long long json_number(const json_value *value)
{
    return value && value->type == JSON_NUMBER ? value->u.number : 0;
}
