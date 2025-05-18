#include <string.h>
#include <stdbool.h>

#include "form.h"

#define FORM_IS_HEX(value) ((value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f'))
#define FORM_CHAR_TO_HEX(value) (value >= '0' && value <= '9' ? value - '0' : value - (value >= 'A' && value <= 'F' ? 'A' : 'a') + 10)

static char form_parse_char(char *, size_t *);

void form_parse(const char *buffer, size_t size, struct form_data *form_data, size_t count)
{
    char *pos = (char *)buffer;
    size_t left = size;
    bool key = false;
    size_t key_len = 0;
    struct form_data *found = form_data;
    size_t found_len = 0;
    while (left > 0) {
        size_t len = left;
        const char current = form_parse_char(pos, &len);
        pos += len;
        left -= len;
        if (key) {
            if (current == '&') {
                if (found) {
                    found->value[found_len++] = 0;
                }
                key = false;
                key_len = 0;
                found = form_data;
                found_len = 0;
            } else if (found) {
                if (found_len < found->value_len - 1) {
                    found->value[found_len++] = current;
                    if (left == 0) {
                        found->value[found_len++] = 0;
                    }
                } else {
                    found->value[found_len++] = 0;
                    found = NULL;
                }
            }
            continue;
        }
        if (current == '=') {
            if (found && key_len != found->key_len) {
                bool clear = true;
                for (size_t i = 0; i < count; i++) {
                    if (form_data[i].key_len == key_len && memcmp(found->key, form_data[i].key, key_len) == 0) {
                        found = &form_data[i];
                        clear = false;
                        break;
                    }
                }
                if (clear) {
                    found = NULL;
                }
            }
            key = true;
            continue;
        }
        if (found) {
            if (found->key[key_len] != current) {
                bool clear = true;
                for (size_t i = 0; i < count; i++) {
                    if (form_data[i].key_len > key_len && form_data[i].key[key_len] == current &&
                        (key_len == 0 || memcmp(found->key, form_data[i].key, key_len) == 0)
                        ) {
                        found = &form_data[i];
                        clear = false;
                        break;
                    }
                }
                if (clear) {
                    found = NULL;
                }
            }
            key_len++;
        }
    }
}

static char form_parse_char(char *value, size_t *size)
{
    char data = *value;
    if (data == '%') {
        if (*size > 3 && FORM_IS_HEX(value[1]) && FORM_IS_HEX(value[2])) {
            *size = 3;
            return FORM_CHAR_TO_HEX(value[1]) << 4 | FORM_CHAR_TO_HEX(value[2]);
        }
    } else if (data == '+') {
        data = ' ';
    }
    *size = 1;
    return data;
}
