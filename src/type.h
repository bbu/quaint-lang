#pragma once

struct lex_symbol;
struct ast_node;

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

enum {
    TYPE_VOID = 0,
    TYPE_U8,
    TYPE_I8,
    TYPE_U16,
    TYPE_I16,
    TYPE_U32,
    TYPE_I32,
    TYPE_U64,
    TYPE_I64,
    TYPE_USIZE,
    TYPE_SSIZE,
    TYPE_UPTR,
    TYPE_IPTR,
    TYPE_PTR,
    TYPE_VPTR,
    TYPE_FPTR,
    TYPE_QUAINT,
    TYPE_STRUCT,
    TYPE_UNION,
    TYPE_ENUM,
    TYPE_COUNT,
};

static_assert(TYPE_COUNT - 1 <= UINT8_MAX, "");
typedef uint8_t type_t;

#define type_is_integral(t) ((t) >= TYPE_U8 && (t) <= TYPE_IPTR)
#define type_is_signed(t) ((t) % 2 == 0)
#define type_is_unsigned(t) ((t) % 2)
#define type_is_ptr(t) ((t) == TYPE_PTR || (t) == TYPE_VPTR || (t) == TYPE_FPTR)
#define type_is_quaint(t) ((t) == TYPE_QUAINT)

#define type_alloc calloc(1, sizeof(struct type))

struct type_nt_pair {
    const struct lex_symbol *name;
    struct type *type;
};

struct type_nv_pair {
    const struct lex_symbol *name;
    uint64_t value;
};

struct type {
    type_t t;
    size_t count;
    size_t size, alignment;

    union {
        /* t == TYPE_PTR || t == TYPE_QUAINT */
        struct type *subtype;

        /* t == TYPE_STRUCT || t == TYPE_UNION */
        struct {
            size_t member_count;
            struct type_nt_pair *members;
            size_t *offsets;
        };

        /* t == TYPE_FPTR */
        struct {
            size_t param_count;
            struct type_nt_pair *params;
            struct type *rettype;
        };

        /* t == TYPE_ENUM */
        struct {
            size_t value_count;
            struct type_nv_pair *values;
            type_t t_value;
        };
    };
};

#define type(_t, _count) &(struct type) {\
    .t = TYPE_##_t, \
    .count = (_count), \
}

#define type_ptr(_count, _subtype) &(struct type) { \
    .t = TYPE_PTR, \
    .count = (_count), \
    .subtype = (_subtype), \
}

#define type_quaint(_count, _subtype) &(struct type) { \
    .t = TYPE_QUAINT, \
    .count = (_count), \
    .subtype = (_subtype), \
}

#define type_struct(_count, _member_count, ...) &(struct type) { \
    .t = TYPE_STRUCT, \
    .count = (_count), \
    .member_count = (_member_count), \
    .members = (struct type_nt_pair[]) { \
        __VA_ARGS__ \
    }, \
}

#define type_union(_count, _member_count, ...) &(struct type) { \
    .t = TYPE_UNION, \
    .count = (_count), \
    .member_count = (_member_count), \
    .members = (struct type_nt_pair[]) { \
        __VA_ARGS__ \
    }, \
}

#define type_fptr(_count, _param_count, _rettype, ...) &(struct type) { \
    .t = TYPE_FPTR, \
    .count = (_count), \
    .param_count = (_param_count), \
    .params = (struct type_nt_pair[]) { \
        __VA_ARGS__ \
    }, \
    .rettype = (_rettype), \
}

#define type_enum(_count, _value_count, _t_value, ...) &(struct type) { \
    .t = TYPE_ENUM, \
    .count = (_count), \
    .value_count = (_value_count), \
    .values = (struct type_nv_pair[]) { \
        __VA_ARGS__ \
    }, \
    .t_value = (_t_value), \
}

struct type_symtab_entry {
    const struct lex_symbol *name;
    struct type *type;
};

type_t type_match(const struct lex_symbol *);
struct type_symtab_entry *type_symtab_find_entry(const struct lex_symbol *);
int type_symtab_insert(const struct type_symtab_entry *);
void type_symtab_clear(void);
void type_print(FILE *, const struct type *);
void type_free(struct type *);
int type_copy(struct type *, const struct type *);
bool type_equals(const struct type *, const struct type *);
int type_quantify(struct type *);
const struct type *type_of_expr(const struct ast_node *);
int type_check_ast(const struct ast_node *);

enum {
    TYPE_OK = 0,
    TYPE_NOMEM,
    TYPE_INVALID,
};
