#pragma once

struct lex_symbol;
struct ast_node;
struct ast_func;

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

enum {
    SCOPE_OBJ_DUPL, // marker for duplicate symbols
    SCOPE_OBJ_BCON, // built-in constant
    SCOPE_OBJ_BFUN, // built-in function
    SCOPE_OBJ_GVAR, // user-defined global variable
    SCOPE_OBJ_AVAR, // user-defined automatic variable
    SCOPE_OBJ_FUNC, // user-defined function
    SCOPE_OBJ_PARM, // user-defined function parameter
    SCOPE_OBJ_COUNT,
};

static_assert(SCOPE_OBJ_COUNT - 1 <= UINT8_MAX, "");
typedef uint8_t scope_obj_t;

enum {
    SCOPE_BCON_ID_NULL,
    SCOPE_BCON_ID_TRUE,
    SCOPE_BCON_ID_FALSE,
    SCOPE_BCON_ID_COUNT,
};

static_assert(SCOPE_BCON_ID_COUNT - 1 <= UINT8_MAX, "");
typedef uint8_t scope_bcon_id_t;

struct scope_builtin_const {
    const struct lex_symbol *const name;
    const struct type *const type;
};

extern const struct scope_builtin_const scope_builtin_consts[];

enum {
    SCOPE_BFUN_ID_NULL = 0,
    SCOPE_BFUN_ID_MONOTIME,
    SCOPE_BFUN_ID_MALLOC,
    SCOPE_BFUN_ID_CALLOC,
    SCOPE_BFUN_ID_REALLOC,
    SCOPE_BFUN_ID_FREE,
    SCOPE_BFUN_ID_PS,
    SCOPE_BFUN_ID_PU8,
    SCOPE_BFUN_ID_PI8,
    SCOPE_BFUN_ID_PU16,
    SCOPE_BFUN_ID_PI16,
    SCOPE_BFUN_ID_PU32,
    SCOPE_BFUN_ID_PI32,
    SCOPE_BFUN_ID_PU64,
    SCOPE_BFUN_ID_PI64,
    SCOPE_BFUN_ID_PNL,
    SCOPE_BFUN_ID_EXIT,
    SCOPE_BFUN_ID_COUNT,
};

static_assert(SCOPE_BFUN_ID_COUNT - 1 <= UINT8_MAX, "");
typedef uint8_t scope_bfun_id_t;

struct scope_builtin_func {
    const struct lex_symbol *const name;
    const struct type *const rettype;
    const size_t param_count;
    const struct type_nt_pair *const params;
};

extern const struct scope_builtin_func scope_builtin_funcs[];

struct scope_obj {
    const struct lex_symbol *name;
    scope_obj_t obj;

    union {
        /* obj == SCOPE_OBJ_GVAR || obj == SCOPE_OBJ_AVAR */
        const struct ast_node *decl;

        /* obj == SCOPE_OBJ_PARM */
        const struct type *type;

        /* obj == SCOPE_OBJ_FUNC */
        const struct ast_node *func;

        /* obj == SCOPE_OBJ_BCON */
        scope_bcon_id_t bcon_id;

        /* obj == SCOPE_OBJ_BFUN */
        scope_bfun_id_t bfun_id;
    };
};

struct scope {
    const struct scope *outer;
    size_t objcount;
    struct scope_obj objs[];
};

int scope_build(struct ast_node *);

enum {
    SCOPE_OK = 0,
    SCOPE_NOMEM,
    SCOPE_DUPLICATED,
};

struct scope_obj *scope_find_object(const struct scope *,
    const struct lex_symbol *);

ptrdiff_t scope_find_wlab(const struct ast_func *, const struct lex_symbol *);
