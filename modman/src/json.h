#ifndef VCOS_JSON_H
#define VCOS_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type;

typedef struct json_value json_value;

struct json_value {
    json_type type;
    union {
        int boolean;
        long long number;
        char *string;
        struct {
            json_value **items;
            size_t count;
        } array;
        struct {
            char **keys;
            json_value **values;
            size_t count;
        } object;
    } u;
};

json_value *json_parse(const char *text, char **error);
void json_free(json_value *value);
const json_value *json_get(const json_value *object, const char *key);
const char *json_string(const json_value *value);
long long json_number(const json_value *value);

#endif
