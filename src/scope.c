#include "scope.h"

#include "lex.h"
#include "ast.h"
#include "type.h"

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define scope_alloc(objcount) \
    calloc(1, sizeof(struct scope) + (objcount) * sizeof(struct scope_obj))

#define NOMEM \
    (fprintf(stderr, "%s:%d: no memory\n", __FILE__, __LINE__), SCOPE_NOMEM)

static inline void aggr_error(int *const error, const int current)
{
    assert(*error >= SCOPE_OK && *error <= SCOPE_DUPLICATED);
    assert(current >= SCOPE_OK && current <= SCOPE_DUPLICATED);

    switch (*error) {
    case SCOPE_OK:
        *error = current;
        break;

    case SCOPE_NOMEM:
        break;

    case SCOPE_DUPLICATED:
        *error = current == SCOPE_NOMEM ? SCOPE_NOMEM : SCOPE_DUPLICATED;
        break;
    }
}

const struct scope_builtin_const scope_builtin_consts[] = {
    {
        lex_sym("null"),
        type(VPTR, 1),
    },

    {
        lex_sym("true"),
        type(U8, 1),
    },

    {
        lex_sym("false"),
        type(U8, 1),
    },
};

static_assert(countof(scope_builtin_consts) == SCOPE_BCON_ID_COUNT, "");

#define PARAMS(...) (const struct type_name_pair []) { \
    __VA_ARGS__ \
}

const struct scope_builtin_func scope_builtin_funcs[] = {
    {
        .name = lex_sym("|"),
        .rettype = NULL,
        .param_count = 0,
        .params = NULL,
    },

    {
        .name = lex_sym("monotime"),
        .rettype = type(U64, 1),
        .param_count = 0,
        .params = NULL,
    },

    {
        .name = lex_sym("malloc"),
        .rettype = type(VPTR, 1),
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("size"),
                .type = type(USIZE, 1),
            }
        )
    },

    {
        .name = lex_sym("calloc"),
        .rettype = type(VPTR, 1),
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("size"),
                .type = type(USIZE, 1),
            }
        )
    },

    {
        .name = lex_sym("realloc"),
        .rettype = type(VPTR, 1),
        .param_count = 2,
        .params = PARAMS
        (
            {
                .name = lex_sym("oldptr"),
                .type = type(VPTR, 1),
            },
            {
                .name = lex_sym("newsize"),
                .type = type(USIZE, 1),
            }
        )
    },

    {
        .name = lex_sym("free"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("ptr"),
                .type = type(VPTR, 1),
            }
        )
    },

    {
        .name = lex_sym("ps"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("str"),
                .type = type_ptr(1, type(U8, 1)),
            }
        )
    },

    {
        .name = lex_sym("pu8"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("num"),
                .type = type(U8, 1),
            }
        )
    },

    {
        .name = lex_sym("pi8"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("num"),
                .type = type(I8, 1),
            }
        )
    },

    {
        .name = lex_sym("pu16"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("num"),
                .type = type(U16, 1),
            }
        )
    },

    {
        .name = lex_sym("pi16"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("num"),
                .type = type(I16, 1),
            }
        )
    },

    {
        .name = lex_sym("pu32"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("num"),
                .type = type(U32, 1),
            }
        )
    },

    {
        .name = lex_sym("pi32"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("num"),
                .type = type(I32, 1),
            }
        )
    },

    {
        .name = lex_sym("pu64"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("num"),
                .type = type(U64, 1),
            }
        )
    },

    {
        .name = lex_sym("pi64"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("num"),
                .type = type(I64, 1),
            }
        )
    },

    {
        .name = lex_sym("pnl"),
        .rettype = NULL,
        .param_count = 0,
        .params = NULL
    },

    {
        .name = lex_sym("exit"),
        .rettype = NULL,
        .param_count = 1,
        .params = PARAMS
        (
            {
                .name = lex_sym("status"),
                .type = type(I32, 1),
            }
        )
    },
};

#undef PARAMS
static_assert(countof(scope_builtin_funcs) == SCOPE_BFUN_ID_COUNT, "bfun mismatch");

static int cmp_scope_obj(const void *const a, const void *const b)
{
    const struct scope_obj *const s1 = a;
    const struct scope_obj *const s2 = b;

    const size_t len1 = (size_t) (s1->name->end - s1->name->beg);
    const size_t len2 = (size_t) (s2->name->end - s2->name->beg);

    char str1[len1 + 1];
    char str2[len2 + 1];

    str1[len1] = '\0';
    str2[len2] = '\0';

    strncpy(str1, (const char *) s1->name->beg, len1);
    strncpy(str2, (const char *) s2->name->beg, len2);

    return strcmp(str1, str2);
}

static int add_func_wlab(struct ast_func *const func,
    const struct ast_node *const node)
{
    const struct ast_wlab *const wlab = ast_data(node, wlab);

    func->wlab_count++;
    void *const tmp = realloc(func->wlabs, sizeof(*func->wlabs) * func->wlab_count);

    if (unlikely(!tmp)) {
        return func->wlab_count = 0, free(func->wlabs), func->wlabs = NULL, NOMEM;
    }

    func->wlabs = tmp;
    func->wlabs[func->wlab_count - 1].name = wlab->name;

    return SCOPE_OK;
}

static inline void identify_wlabs(const struct ast_func *const func)
{
    uint64_t wlab_id = 1;

    for (size_t idx = 0; idx < func->wlab_count; wlab_id++) {
        const struct lex_symbol *const outer_name = func->wlabs[idx].name;
        func->wlabs[idx++].id = wlab_id;

        while (idx < func->wlab_count &&
            lex_symbols_equal(outer_name, func->wlabs[idx].name)) {

            func->wlabs[idx++].id = wlab_id;
        }
    }
}

static inline size_t count_objects_unit(const struct ast_unit *const unit)
{
    size_t objcount = SCOPE_BCON_ID_COUNT + SCOPE_BFUN_ID_COUNT;

    for (size_t idx = 0; idx < unit->stmt_count; ++idx) {
        const struct ast_node *const stmt = unit->stmts[idx];

        if (unlikely(!stmt)) {
            continue;
        }

        if (stmt->an == AST_AN_DECL) {
            objcount += ast_data(stmt, decl)->name_count;
        } else if (stmt->an == AST_AN_FUNC) {
            objcount++;
        }
    }

    return objcount;
}

static size_t count_objects_block(const struct ast_node *const node)
{
    struct ast_node *const *stmts;
    size_t stmt_count;
    size_t objcount = 0;

    switch (node->an) {
    case AST_AN_BLOK:
    case AST_AN_NOIN: {
        const struct ast_blok *const blok = ast_data(node, blok);
        stmts = blok->stmts, stmt_count = blok->stmt_count;
    } break;

    case AST_AN_WHIL:
    case AST_AN_DOWH: {
        const struct ast_whil *const whil = ast_data(node, whil);
        stmts = whil->stmts, stmt_count = whil->stmt_count;
    } break;

    case AST_AN_FUNC: {
        const struct ast_func *const func = ast_data(node, func);
        stmts = func->stmts, stmt_count = func->stmt_count;
        objcount += func->param_count;
    } break;

    default: assert(0), abort();
    }

    for (size_t idx = 0; idx < stmt_count; ++idx) {
        if (stmts[idx] && stmts[idx]->an == AST_AN_DECL) {
            objcount += ast_data(stmts[idx], decl)->name_count;
        }
    }

    return objcount;
}

static int find_duplicates(struct scope_obj *const scope_objs,
    const size_t objcount)
{
    bool outer_dup = false;

    for (size_t outer_idx = 0; outer_idx < objcount; ++outer_idx) {
        struct scope_obj *const so_outer = &scope_objs[outer_idx];

        if (so_outer->obj == SCOPE_OBJ_DUPL) {
            continue;
        }

        bool inner_dup = false;

        for (size_t inner_idx = outer_idx + 1; inner_idx < objcount; ++inner_idx) {
            struct scope_obj *const so_inner = &scope_objs[inner_idx];

            if (unlikely(lex_symbols_equal(so_outer->name, so_inner->name))) {
                outer_dup = inner_dup = true;

                const struct lex_token *const tok =
                    (const struct lex_token *) (so_inner)->name;

                lex_print_error(stderr, "duplicate declaration", tok, tok);
                so_inner->obj = SCOPE_OBJ_DUPL;
            }
        }

        if (inner_dup) {
            so_outer->obj = SCOPE_OBJ_DUPL;
        }
    }

    return outer_dup ? SCOPE_DUPLICATED : SCOPE_OK;
}

static inline size_t add_builtins(const struct ast_unit *const unit)
{
    size_t offset = 0;

    for (scope_bcon_id_t id = 0; id < SCOPE_BCON_ID_COUNT; ++id) {
        unit->scope->objs[offset++] = (struct scope_obj) {
            .name = scope_builtin_consts[id].name,
            .obj = SCOPE_OBJ_BCON,
            .bcon_id = id,
        };
    }

    for (scope_bfun_id_t id = 0; id < SCOPE_BFUN_ID_COUNT; ++id) {
        unit->scope->objs[offset++] = (struct scope_obj) {
            .name = scope_builtin_funcs[id].name,
            .obj = SCOPE_OBJ_BFUN,
            .bfun_id = id,
        };
    }

    return offset;
}

static int scope_build_inner(struct ast_node *const node,
    const struct scope *const outer)
{
    assert(node != NULL);

    static struct ast_func *outer_func;
    int error = SCOPE_OK;

    switch (node->an) {
    case AST_AN_FUNC: {
        struct ast_func *const func = ast_data(node, func);
        const size_t objcount = count_objects_block(node);

        assert(func->scope == NULL);

        if (unlikely(!(func->scope = scope_alloc(objcount)))) {
            return NOMEM;
        }

        func->scope->outer = outer;
        func->scope->objcount = objcount;
        size_t offset = 0;

        for (size_t param_idx = 0; param_idx < func->param_count; ++param_idx) {
            func->scope->objs[offset++] = (struct scope_obj) {
                .name = func->params[param_idx].name,
                .obj = SCOPE_OBJ_PARM,
                .type = func->params[param_idx].type,
            };
        }

        outer_func = func;

        for (size_t idx = 0; idx < func->stmt_count; ++idx) {
            struct ast_node *const stmt = func->stmts[idx];

            if (unlikely(!stmt)) {
                continue;
            }

            if (stmt->an == AST_AN_DECL) {
                const struct ast_decl *const decl = ast_data(stmt, decl);

                for (size_t name_idx = 0; name_idx < decl->name_count; ++name_idx) {
                    func->scope->objs[offset++] = (struct scope_obj) {
                        .name = decl->names[name_idx],
                        .obj = SCOPE_OBJ_AVAR,
                        .decl = stmt,
                    };
                }
            } else if (stmt->an == AST_AN_WLAB) {
                aggr_error(&error, add_func_wlab(func, stmt));
            } else {
                aggr_error(&error, scope_build_inner(stmt, func->scope));
            }
        }

        qsort(func->wlabs, func->wlab_count, sizeof(*func->wlabs), cmp_scope_obj);
        identify_wlabs(func);
        aggr_error(&error, find_duplicates(func->scope->objs, objcount));
        qsort(func->scope->objs, objcount, sizeof(struct scope_obj), cmp_scope_obj);
    } break;

    case AST_AN_BLOK:
    case AST_AN_NOIN:
    case AST_AN_WHIL:
    case AST_AN_DOWH: {
        struct scope **scope;
        size_t stmt_count;
        struct ast_node **stmts;
        const size_t objcount = count_objects_block(node);

        if (node->an == AST_AN_BLOK || node->an == AST_AN_NOIN) {
            struct ast_blok *const blok = ast_data(node, blok);
            scope = &blok->scope;
            stmt_count = blok->stmt_count;
            stmts = blok->stmts;
        } else {
            struct ast_whil *const whil = ast_data(node, whil);
            scope = &whil->scope;
            stmt_count = whil->stmt_count;
            stmts = whil->stmts;
        }

        assert(*scope == NULL);

        if (unlikely(!(*scope = scope_alloc(objcount)))) {
            return NOMEM;
        }

        (*scope)->outer = outer;
        (*scope)->objcount = objcount;

        for (size_t idx = 0, offset = 0; idx < stmt_count; ++idx) {
            struct ast_node *const stmt = stmts[idx];

            if (unlikely(!stmt)) {
                continue;
            }

            if (stmt->an == AST_AN_DECL) {
                const struct ast_decl *const decl = ast_data(stmt, decl);

                for (size_t name_idx = 0; name_idx < decl->name_count; ++name_idx) {
                    (*scope)->objs[offset++] = (struct scope_obj) {
                        .name = decl->names[name_idx],
                        .obj = SCOPE_OBJ_AVAR,
                        .decl = stmt,
                    };
                }
            } else if (stmt->an == AST_AN_WLAB) {
                aggr_error(&error, add_func_wlab(outer_func, stmt));
            } else {
                aggr_error(&error, scope_build_inner(stmt, *scope));
            }
        }

        aggr_error(&error, find_duplicates((*scope)->objs, objcount));
        qsort((*scope)->objs, objcount, sizeof(struct scope_obj), cmp_scope_obj);
    } break;

    case AST_AN_COND: {
        struct ast_cond *const cond = ast_data(node, cond);

        aggr_error(&error, scope_build_inner(cond->if_block, outer));

        for (size_t idx = 0; idx < cond->elif_count; ++idx) {
            aggr_error(&error, scope_build_inner(cond->elif[idx].block, outer));
        }

        if (cond->else_block) {
            aggr_error(&error, scope_build_inner(cond->else_block, outer));
        }
    } break;
    }

    return error;
}

int scope_build(struct ast_node *const root)
{
    assert(root != NULL);

    struct ast_unit *const unit = ast_data(root, unit);
    size_t objcount = count_objects_unit(unit);

    assert(unit->scope == NULL);

    if (unlikely(!(unit->scope = scope_alloc(objcount)))) {
        return NOMEM;
    }

    unit->scope->objcount = objcount;
    size_t offset = add_builtins(unit);
    int error = SCOPE_OK;

    for (size_t idx = 0; idx < unit->stmt_count; ++idx) {
        struct ast_node *const stmt = unit->stmts[idx];

        if (unlikely(!stmt)) {
            continue;
        }

        if (stmt->an == AST_AN_DECL) {
            const struct ast_decl *const decl = ast_data(stmt, decl);

            for (size_t name_idx = 0; name_idx < decl->name_count; ++name_idx) {
                unit->scope->objs[offset++] = (struct scope_obj) {
                    .name = decl->names[name_idx],
                    .obj = SCOPE_OBJ_GVAR,
                    .decl = stmt,
                };
            }
        } else if (stmt->an == AST_AN_FUNC) {
            const struct ast_func *const func = ast_data(stmt, func);

            unit->scope->objs[offset++] = (struct scope_obj) {
                .name = func->name,
                .obj = SCOPE_OBJ_FUNC,
                .func = stmt,
            };

            aggr_error(&error, scope_build_inner(stmt, unit->scope));
        }
    }

    aggr_error(&error, find_duplicates(unit->scope->objs, objcount));
    qsort(unit->scope->objs, objcount, sizeof(struct scope_obj), cmp_scope_obj);
    return error;
}

struct scope_obj *scope_find_object(const struct scope *scope,
    const struct lex_symbol *const name)
{
    const struct scope_obj needle = { .name = name };

    do {
        struct scope_obj *const found = bsearch(&needle, scope->objs,
            scope->objcount, sizeof(struct scope_obj), cmp_scope_obj);

        if (found && found->obj != SCOPE_OBJ_DUPL) {
            if (found->obj == SCOPE_OBJ_AVAR) {
                const struct ast_decl *const decl = ast_data(found->decl, decl);
                return likely(name->beg > decl->names[0]->beg) ? found : NULL;
            } else {
                return found;
            }
        }
    } while ((scope = scope->outer));

    return NULL;
}

ptrdiff_t scope_find_wlab(const struct ast_func *const func,
    const struct lex_symbol *const name)
{
    const struct scope_obj needle = { .name = name };
    const ssize_t elem_size = sizeof(*func->wlabs);

    const void *const found = bsearch(&needle, func->wlabs, func->wlab_count,
        (size_t) elem_size, cmp_scope_obj);

    return likely(found) ?
        ((char *) found - (char *) func->wlabs) / elem_size : -1;
}
