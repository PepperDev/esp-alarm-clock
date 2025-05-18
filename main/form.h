#ifndef _FORM_H
#define _FORM_H

#include <stddef.h>

struct form_data {
    const char *key;
    size_t key_len;
    char *value;
    size_t value_len;
} __attribute__((packed));

void form_parse(const char *, size_t, struct form_data *, size_t);

#endif
