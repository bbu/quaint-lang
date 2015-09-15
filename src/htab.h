#pragma once

#include <stddef.h>
#include <stdint.h>

struct htab {
    size_t count;

    struct {
        uintptr_t key;
        const void *data;
    } items[];
};

int htab_create(struct htab **, size_t);
void htab_destroy(const struct htab *, void (*)(const void *));
void htab_default_dtor(const void *);
void htab_insert(struct htab *, uintptr_t, const void *);
void *htab_get(const struct htab *, uintptr_t);

enum {
    HTAB_OK = 0,
    HTAB_NOMEM,
};
