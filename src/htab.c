#include "htab.h"

#include "common.h"

#include <stdlib.h>
#include <assert.h>

#define HASH(key, count) ((key) % ((count) ?: 1))

int htab_create(struct htab **const htab, const size_t count)
{
    assert(htab != NULL);
    *htab = calloc(1, sizeof(struct htab) + count * sizeof((*htab)->items[0]));

    if (unlikely(!*htab)) {
        return HTAB_NOMEM;
    }

    (*htab)->count = count;
    return HTAB_OK;
}

void htab_destroy(const struct htab *const htab, void (*const dtor)(const void *))
{
    if (likely(htab && dtor)) {
        for (size_t idx = 0; idx < htab->count; ++idx) {
            dtor(htab->items[idx].data);
        }
    }

    free((struct htab *) htab);
}

void htab_default_dtor(const void *const data)
{
    free((void *) data);
}

void htab_insert(struct htab *const htab, const uintptr_t key, const void *const data)
{
    assert(htab != NULL);
    assert("zero key passed" && key);

    const size_t pos = HASH(key, htab->count);
    size_t idx;

    for (idx = pos; idx < htab->count; ++idx) {
        assert("duplicate key" && htab->items[idx].key != key);

        if (likely(!htab->items[idx].key)) {
            goto insert;
        }
    }

    for (idx = 0; idx < pos; ++idx) {
        assert("duplicate key" && htab->items[idx].key != key);

        if (likely(!htab->items[idx].key)) {
            goto insert;
        }
    }

    assert(0), abort();

insert:
    htab->items[idx].key = key;
    htab->items[idx].data = data;
}

void *htab_get(const struct htab *const htab, const uintptr_t key)
{
    assert(htab != NULL);
    assert("zero key passed" && key);

    const size_t pos = HASH(key, htab->count);
    size_t idx;

    for (idx = pos; idx < htab->count; ++idx) {
        if (likely(htab->items[idx].key == key)) {
            goto get;
        }
    }

    for (idx = 0; idx < pos; ++idx) {
        if (likely(htab->items[idx].key == key)) {
            goto get;
        }
    }

    assert(0), abort();

get:
    return (void *) htab->items[idx].data;
}
