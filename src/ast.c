#include "ast.h"

#include "lex.h"
#include "parse.h"
#include "scope.h"
#include "type.h"

#include "common.h"

#include <stdlib.h>
#include <assert.h>

#define NOMEM \
    (fprintf(stderr, "%s:%d: no memory\n", __FILE__, __LINE__), AST_NOMEM)

#define INVALID(d, n) ({ \
    const struct lex_token *ltok, *rtok; \
    parse_node_ltok_rtok((n), &ltok, &rtok); \
    lex_print_error(stderr, (d), ltok, rtok); \
    AST_INVALID; \
})

#define INVALID_TK(d, t) ({ \
    lex_print_error(stderr, (d), (t), (t)); \
    AST_INVALID; \
})

enum {
    CTX_UNIT,
    CTX_FUNC,
    CTX_BLOCK,
    CTX_COUNT,
};

static_assert(CTX_COUNT <= UINT8_MAX, "");
typedef uint8_t ctx_t;

static struct {
    uint8_t size;
    ctx_t contexts[CTX_COUNT];
} ctx_stack;

static inline void push_context(const ctx_t ctx)
{
    assert(ctx_stack.size < CTX_COUNT);
    ctx_stack.contexts[ctx_stack.size++] = ctx;
}

static inline void pop_context(void)
{
    assert(ctx_stack.size >= 1);
    --ctx_stack.size;
}

static inline ctx_t top_context(void)
{
    return ctx_stack.contexts[ctx_stack.size - 1];
}

static inline void aggr_error(int *const error, const int current)
{
    assert(*error >= AST_OK && *error <= AST_INVALID);
    assert(current >= AST_OK && current <= AST_INVALID);

    switch (*error) {
    case AST_OK:
        *error = current;
        break;

    case AST_NOMEM:
        break;

    case AST_INVALID:
        *error = current == AST_NOMEM ? AST_NOMEM : AST_INVALID;
        break;
    }
}

static uint64_t nmbr_to_uint(const struct parse_node *const nmbr)
{
    const uint8_t *const beg = nmbr->token->beg;
    const uint8_t *const end = nmbr->token->end;
    uint64_t result = 0, mult = 1;

    for (ptrdiff_t idx = end - beg - 1; idx >= 0; --idx, mult *= 10) {
        result += mult * (beg[idx] - '0');
    }

    return result;
}

static inline int expr_is_atom(const lex_tk_t tk,
    const struct parse_node *const expr)
{
    return parse_node_nt(expr->children[0]) == PARSE_NT_Atom &&
        parse_node_tk(expr->children[0]->children[0]) == tk;
}

static inline int expr_is(const parse_nt_t nt, const struct parse_node *const expr)
{
    return parse_node_nt(expr->children[0]) == nt;
}

static int resolve_type(struct type *unresolved,
    const struct parse_node *const name)
{
    const struct type_symtab_entry *const entry =
        type_symtab_find_entry((struct lex_symbol *) name->token);

    if (unlikely(!entry)) {
        return INVALID("reference to undefined type", name);
    }

    const size_t count = unresolved->count;

    return unlikely(type_copy(unresolved, entry->type)) ?
        NOMEM : (unresolved->count = count, AST_OK);
}

static inline void count_elif_else(const struct parse_node *const ctrl,
    size_t *const elif_count, size_t *const else_idx)
{
    *elif_count = *else_idx = 0;

    if (ctrl->nchildren != 1) {
        const struct parse_node *child = ctrl->children[1];
        const size_t nchildren = ctrl->nchildren;

        while (1 + *elif_count < nchildren &&
            parse_node_nt(child) == PARSE_NT_Elif) {

            (*elif_count)++;
            child++;
        }

        if (1 + *elif_count != nchildren) {
            *else_idx = nchildren - 1;
        }
    }
}

static inline const struct parse_node *count_decl_qualifiers(
    const struct parse_node *child, size_t *const qual_count,
    uint8_t *const cons, uint8_t *const expo, uint8_t *const stat)
{
    *qual_count = 0;
    *cons = 0, *expo = 0, *stat = 0;
    const ctx_t ctx = top_context();

    for (; parse_node_nt(child) == PARSE_NT_Qual; ++child, ++*qual_count) {
        switch (parse_node_tk(child->children[0])) {
        case LEX_TK_CONS:
            if (*cons) {
                return INVALID("duplicate qualifier", child), NULL;
            }

            *cons = 1;
            break;

        case LEX_TK_EXPO:
            if (ctx != CTX_UNIT) {
                return INVALID("qualifier not in unit context", child), NULL;
            }

            if (*expo) {
                return INVALID("duplicate qualifier", child), NULL;
            }

            *expo = 1;
            break;

        case LEX_TK_STAT:
            if (ctx == CTX_UNIT) {
                return INVALID("qualifier in unit context", child), NULL;
            }

            if (*stat) {
                return INVALID("duplicate qualifier", child), NULL;
            }

            *stat = 1;
            break;

        default: assert(0), abort();
        }
    }

    return child;
}

static int validate_typespec(const struct parse_node *, struct type *);

static int validate_type_name_pairs(const struct parse_node *,
    struct type_name_pair **, size_t *);

static int validate_blok(const struct parse_node *,
    struct ast_node **, const struct ast_node *);

static int validate_func(const struct parse_node *,
    struct ast_node **, const struct ast_node *);

static int validate_ctrl(const struct parse_node *,
    struct ast_node **, const struct ast_node *);

static int validate_cond(const struct parse_node *,
    struct ast_node **, const struct ast_node *);

static int validate_whil(const struct parse_node *,
    struct ast_node **, const struct ast_node *);

static int validate_dowh(const struct parse_node *,
    struct ast_node **, const struct ast_node *);

static int validate_type(const struct parse_node *, struct ast_node **,
    const struct ast_node *);

static int validate_retn(const struct parse_node *, struct ast_node **,
    const struct ast_node *);

static int validate_wait(const struct parse_node *, struct ast_node **,
    const struct ast_node *);

static int validate_expr(const struct parse_node *,
    struct ast_node **, const struct ast_node *);

static int validate_decl_or_expr(const struct parse_node *,
    struct ast_node **, const struct ast_node *);

static int validate_wlab(const struct parse_node *, struct ast_node **,
    const struct ast_node *);

static int validate_stmt(const struct parse_node *const stmt,
    struct ast_node **, const struct ast_node *);

#define ast_alloc_node(_node, extra) \
    calloc(1, sizeof(struct ast_node) + sizeof(struct ast_##_node) + (extra))

#define ast_set_node(node, a, p, pn) ({ \
    (node)->an = (a); \
    (node)->parent = (p); \
    parse_node_ltok_rtok((pn), &(node)->ltok, &(node)->rtok); \
})

#define validate_stmts(stmt_count, off, nodes, stmts, parent) ({ \
    int err = AST_OK; \
    \
    for (size_t idx = 0; idx < (stmt_count); ++idx) { \
        aggr_error(&err, \
            validate_stmt((nodes)[off + idx], &(stmts)[idx], (parent))); \
    } \
    \
    err; \
})

static int validate_type_name_pairs(const struct parse_node *expr,
    struct type_name_pair **const pairs, size_t *const count)
{
    struct type *type;
    int error;
    struct type_name_pair *tmp;

    for (;;) {
        if (!expr_is(PARSE_NT_Bexp, expr)) {
            error = INVALID("bad name-type pair", expr);
            goto fail_free;
        }

        const struct parse_node *const bexp = expr->children[0];
        const struct parse_node *const left = bexp->children[0];
        const struct parse_node *const op = bexp->children[1];
        const struct parse_node *const right = bexp->children[2];

        if (parse_node_tk(op) != LEX_TK_COMA) {
            if (parse_node_tk(op) != LEX_TK_COLN) {
                error = INVALID("expecting a colon", op);
                goto fail_free;
            }

            if (!expr_is_atom(LEX_TK_NAME, left)) {
                error = INVALID("expecting a name", left);
                goto fail_free;
            }

            if (unlikely(!(type = type_alloc))) {
                error = NOMEM;
                goto fail_free;
            }

            if ((error = validate_typespec(right, type))) {
                type_free(type);
                goto fail_free;
            }

            if (unlikely(!(tmp = realloc(*pairs, (*count + 1) *
                sizeof(struct type_name_pair))))) {

                type_free(type), error = NOMEM;
                goto fail_free;
            }

            (*pairs = tmp)[(*count)++] = (struct type_name_pair) {
                .type = type,
                .name = (struct lex_symbol *) left->children[0]->children[0]->token,
            };

            break;
        }

        if (!expr_is(PARSE_NT_Bexp, left)) {
            error = INVALID("bad name-type pair", left);
            goto fail_free;
        }

        const struct parse_node *const left_bexp = left->children[0];
        const struct parse_node *const left_left = left_bexp->children[0];
        const struct parse_node *const left_op = left_bexp->children[1];
        const struct parse_node *const left_right = left_bexp->children[2];

        if (parse_node_tk(left_op) != LEX_TK_COLN) {
            error = INVALID("expecting a colon", left_op);
            goto fail_free;
        }

        if (!expr_is_atom(LEX_TK_NAME, left_left)) {
            error = INVALID("expecting a name", left_left);
            goto fail_free;
        }

        if (unlikely(!(type = type_alloc))) {
            error = NOMEM;
            goto fail_free;
        }

        if ((error = validate_typespec(left_right, type))) {
            type_free(type);
            goto fail_free;
        }

        if (unlikely(!(tmp = realloc(*pairs, (*count + 1) *
            sizeof(struct type_name_pair))))) {

            type_free(type), error = NOMEM;
            goto fail_free;
        }

        (*pairs = tmp)[(*count)++] = (struct type_name_pair) {
            .type = type,
            .name = (struct lex_symbol *) left_left->children[0]->children[0]->token,
        };

        expr = right;
    }

    for (size_t i = 0; i < *count; ++i) {
        for (size_t j = i + 1; j < *count; ++j) {
            if (unlikely(lex_symbols_equal((*pairs)[i].name, (*pairs)[j].name))) {
                const struct lex_token *const token = (struct lex_token *)
                    (*pairs)[j].name;

                error = INVALID_TK("duplicate name in type-name list", token);
                goto fail_free;
            }
        }
    }

    return AST_OK;

fail_free:
    for (size_t idx = 0; idx < *count; ++idx) {
        type_free((*pairs)[idx].type);
    }

    *count = 0, free(*pairs), *pairs = NULL;
    return error;
}

static int validate_typespec(const struct parse_node *const node,
    struct type *const type)
{
    int error;

    switch (node->nt) {
    case PARSE_NT_Atom: {
        const struct parse_node *const child = node->children[0];

        if (parse_node_tk(child) != LEX_TK_NAME) {
            return INVALID("bad type name", child);
        }

        type->count = 1;

        switch ((type->t = type_match((struct lex_symbol *) child->token))) {
        case TYPE_PTR:
            return INVALID("pointer must have a subtype", child);

        case TYPE_FPTR:
            return INVALID("function pointer must list its arguments", child);

        case TYPE_QUAINT:
            return INVALID("quaint must have a subtype", child);

        case TYPE_STRUCT:
            return INVALID("struct must have members", child);

        case TYPE_UNION:
            return INVALID("union must have members", child);

        case TYPE_VOID:
            return resolve_type(type, child);
        }

        return AST_OK;
    }

    case PARSE_NT_Fexp: {
        const struct parse_node *const left = node->children[0];
        const struct parse_node *const right =
            node->nchildren == 4 ? node->children[2] : NULL;

        const struct parse_node *pritype;

        if (expr_is(PARSE_NT_Aexp, left)) {
            const struct parse_node *const aexp = left->children[0];
            const struct parse_node *const array_type = aexp->children[0];
            const struct parse_node *const array_size = aexp->children[2];

            if (!expr_is_atom(LEX_TK_NAME, array_type)) {
                return INVALID("bad array type", array_type);
            } else if (!expr_is_atom(LEX_TK_NMBR, array_size)) {
                return INVALID("bad array size", array_size);
            }

            type->count = (size_t) nmbr_to_uint(array_size->children[0]->children[0]);
            pritype = array_type;
        } else if (expr_is_atom(LEX_TK_NAME, left)) {
            type->count = 1;
            pritype = left;
        } else {
            return INVALID("bad type expression", left);
        }

        const struct lex_symbol *const type_name = (struct lex_symbol *)
            pritype->children[0]->children[0]->token;

        switch ((type->t = type_match(type_name))) {
        case TYPE_PTR:
        case TYPE_QUAINT: {
            if (!right && type->t == TYPE_PTR) {
                return INVALID("pointer must have a subtype", node);
            }

            if (unlikely(!(type->subtype = type_alloc))) {
                return NOMEM;
            }

            return right ? validate_typespec(right, type->subtype) : AST_OK;
        }

        case TYPE_FPTR: {
            return right ? validate_type_name_pairs(right,
                &type->params, &type->param_count) : AST_OK;
        }

        case TYPE_STRUCT:
        case TYPE_UNION: {
            if (!right) {
                if (type->t == TYPE_STRUCT) {
                    return INVALID("struct must have members", node);
                } else {
                    return INVALID("union must have members", node);
                }
            }

            return validate_type_name_pairs(right,
                &type->members, &type->member_count);
        }

        case TYPE_VOID:
            return INVALID("bad builtin type", node);

        default:
            return INVALID("builtin type must not have a subtype", node);
        }
    }

    case PARSE_NT_Aexp: {
        const struct parse_node *const array_type = node->children[0];
        const struct parse_node *const array_size = node->children[2];

        if (!expr_is_atom(LEX_TK_NAME, array_type)) {
            return INVALID("bad array type", array_type);
        } else if (!expr_is_atom(LEX_TK_NMBR, array_size)) {
            return INVALID("bad array size", array_size);
        }

        const struct lex_symbol *const type_name = (struct lex_symbol *)
            array_type->children[0]->children[0]->token;

        type->count = (size_t) nmbr_to_uint(array_size->children[0]->children[0]);

        switch ((type->t = type_match(type_name))) {
        case TYPE_PTR:
            return INVALID("array of pointers must have a subtype", node);

        case TYPE_FPTR:
            return INVALID("array of function pointers must list its arguments", node);

        case TYPE_QUAINT:
            return INVALID("array of quaints must have a subtype", node);

        case TYPE_STRUCT:
            return INVALID("array of structs must have members", node);

        case TYPE_UNION:
            return INVALID("array of unions must have members", node);

        case TYPE_VOID:
            return resolve_type(type, array_type->children[0]->children[0]);
        }

        return AST_OK;
    }

    case PARSE_NT_Bexp: {
        const struct parse_node *const op = node->children[1];

        if (parse_node_tk(op) != LEX_TK_COLN) {
            return INVALID("bad type expression", node);
        }

        const struct parse_node *const left = node->children[0];
        const struct parse_node *const right = node->children[2];

        if (!expr_is(PARSE_NT_Fexp, left)) {
            return INVALID("expecting a functional expression", left);
        }

        const struct parse_node *const fexp = left->children[0];
        const struct parse_node *const fexp_left = fexp->children[0];
        const struct parse_node *const fexp_right =
            fexp->nchildren == 4 ? fexp->children[2] : NULL;

        const struct parse_node *pritype;

        if (expr_is(PARSE_NT_Aexp, fexp_left)) {
            const struct parse_node *const aexp = fexp_left->children[0];
            const struct parse_node *const array_type = aexp->children[0];
            const struct parse_node *const array_size = aexp->children[2];

            if (!expr_is_atom(LEX_TK_NAME, array_type)) {
                return INVALID("bad array type", array_type);
            } else if (!expr_is_atom(LEX_TK_NMBR, array_size)) {
                return INVALID("bad array size", array_size);
            }

            type->count = (size_t) nmbr_to_uint(array_size->children[0]->children[0]);
            pritype = array_type;
        } else if (expr_is_atom(LEX_TK_NAME, fexp_left)) {
            type->count = 1;
            pritype = fexp_left;
        } else {
            return INVALID("bad type expression", fexp_left);
        }

        const struct lex_symbol *const type_name = (struct lex_symbol *)
            pritype->children[0]->children[0]->token;

        if ((type->t = type_match(type_name)) != TYPE_FPTR) {
            return INVALID("expecting a function pointer", pritype);
        }

        if (fexp_right) {
            if ((error = validate_type_name_pairs(fexp_right,
                &type->params, &type->param_count))) {

                return error;
            }
        }

        if (unlikely(!(type->rettype = type_alloc))) {
            return NOMEM;
        }

        return validate_typespec(right, type->rettype);
    }

    case PARSE_NT_Expr: {
        return validate_typespec(node->children[0], type);
    }

    default:
        return INVALID("bad type specifier", node);
    }
}

static int validate_wlab(const struct parse_node *const stmt,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    const struct parse_node *const expr = stmt->children[1];

    if (!expr_is_atom(LEX_TK_NAME, expr)) {
        return INVALID("expecting a label name", expr);
    }

    if (unlikely(!(*ast = ast_alloc_node(wlab, 0)))) {
        return NOMEM;
    }

    ast_set_node(*ast, AST_AN_WLAB, parent, stmt);
    ast_data(*ast, wlab)->name = (struct lex_symbol *)
        expr->children[0]->children[0]->token;

    return AST_OK;
}

static int validate_blok(const struct parse_node *const blok,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    const uint8_t noint = parse_node_tk(blok->children[0]) == LEX_TK_NOIN;
    const size_t stmt_count = blok->nchildren - 2 - noint;

    if (unlikely(!(*ast = ast_alloc_node(blok,
        stmt_count * sizeof(struct ast_node *))))) {

        return NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    struct ast_blok *const ast_blok = ast_data(*ast, blok);

    ast_set_node(ast_node, noint ? AST_AN_NOIN : AST_AN_BLOK, parent, blok);
    ast_blok->stmt_count = stmt_count;

    return validate_stmts(stmt_count, noint + 1,
        blok->children, ast_blok->stmts, ast_node);
}

static int validate_func(const struct parse_node *const func,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    int error;
    const struct parse_node *child = func->children[0];
    size_t qual_count = 0;
    uint8_t exposed = 0;

    for (; parse_node_nt(child) == PARSE_NT_Qual; ++child, ++qual_count) {
        switch (parse_node_tk(child->children[0])) {
        case LEX_TK_CONS:
        case LEX_TK_STAT: {
            return INVALID("invalid qualifier for function", child);
        }

        case LEX_TK_EXPO: {
            if (exposed) {
                return INVALID("duplicate qualifier", child);
            }

            exposed = 1;
        } break;

        default: assert(0), abort();
        }
    }

    const size_t stmt_count = func->nchildren - qual_count - 3;

    if (unlikely(!(*ast = ast_alloc_node(func,
        stmt_count * sizeof(struct ast_node *))))) {

        return NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    struct ast_func *const ast_func = ast_data(*ast, func);

    if (expr_is_atom(LEX_TK_NAME, child)) {
        ast_func->name = (struct lex_symbol *)
            child->children[0]->children[0]->token;
    } else if (expr_is(PARSE_NT_Bexp, child)) {
        const struct parse_node *const bexp = child->children[0];
        const struct parse_node *const left = bexp->children[0];
        const struct parse_node *const op = bexp->children[1];
        const struct parse_node *const right = bexp->children[2];

        if (parse_node_tk(op) != LEX_TK_COLN) {
            return INVALID("expecting a colon", op);
        }

        if (expr_is_atom(LEX_TK_NAME, left)) {
            ast_func->name = (struct lex_symbol *)
                left->children[0]->children[0]->token;
        } else if (expr_is(PARSE_NT_Fexp, left)) {
            const struct parse_node *const fexp = left->children[0];
            const struct parse_node *const fexp_left = fexp->children[0];
            const struct parse_node *const fexp_right = fexp->nchildren == 4 ?
                fexp->children[2] : NULL;

            if (!expr_is_atom(LEX_TK_NAME, fexp_left)) {
                return INVALID("bad function name", fexp_left);
            }

            ast_func->name = (struct lex_symbol *)
                fexp_left->children[0]->children[0]->token;

            if (fexp_right && (error = validate_type_name_pairs(fexp_right,
                &ast_func->params, &ast_func->param_count))) {

                return error;
            }
        } else {
            return INVALID("bad function signature", left);
        }

        if (unlikely(!(ast_func->rettype = type_alloc))) {
            return NOMEM;
        }

        if ((error = validate_typespec(right, ast_func->rettype))) {
            return type_free(ast_func->rettype), error;
        }
    } else if (expr_is(PARSE_NT_Fexp, child)) {
        const struct parse_node *const fexp = child->children[0];
        const struct parse_node *const fexp_left = fexp->children[0];
        const struct parse_node *const fexp_right = fexp->nchildren == 4 ?
            fexp->children[2] : NULL;

        if (!expr_is_atom(LEX_TK_NAME, fexp_left)) {
            return INVALID("bad function name", fexp_left);
        }

        ast_func->name = (struct lex_symbol *)
            fexp_left->children[0]->children[0]->token;

        if (fexp_right && (error = validate_type_name_pairs(fexp_right,
            &ast_func->params, &ast_func->param_count))) {

            return error;
        }
    } else {
        return INVALID("bad function signature", child);
    }

    ast_set_node(ast_node, AST_AN_FUNC, parent, func);
    ast_func->expo = exposed;
    ast_func->stmt_count = stmt_count;

    return validate_stmts(stmt_count, qual_count + 2,
        func->children, ast_func->stmts, ast_node);
}

static int validate_ctrl(const struct parse_node *const ctrl,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    switch (parse_node_nt(ctrl->children[0])) {
    case PARSE_NT_Cond:
        return validate_cond(ctrl, ast, parent);

    case PARSE_NT_Whil:
        return validate_whil(ctrl->children[0], ast, parent);

    case PARSE_NT_Dowh:
        return validate_dowh(ctrl->children[0], ast, parent);

    default: assert(0), abort();
    }
}

static int validate_cond(const struct parse_node *const ctrl,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    size_t elif_count, else_idx;
    count_elif_else(ctrl, &elif_count, &else_idx);

    if (unlikely(!(*ast = ast_alloc_node(cond,
        elif_count * sizeof(((struct ast_cond *) NULL)->elif[0]))))) {

        return NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    struct ast_cond *const ast_cond = ast_data(*ast, cond);

    ast_set_node(ast_node, AST_AN_COND, parent, ctrl);
    ast_cond->elif_count = elif_count;

    const struct parse_node *const cond = ctrl->children[0];
    int error = validate_expr(cond->children[1], &ast_cond->if_expr, ast_node);
    const size_t if_stmt_count = cond->nchildren - 4;

    if (unlikely(!(ast_cond->if_block = ast_alloc_node(blok,
        if_stmt_count * sizeof(struct ast_node *))))) {

        aggr_error(&error, NOMEM);
    } else {
        ast_set_node(ast_cond->if_block, AST_AN_BLOK, ast_node, cond);
        ast_data(ast_cond->if_block, blok)->stmt_count = if_stmt_count;

        aggr_error(&error, validate_stmts(if_stmt_count, 3, cond->children,
            ast_data(ast_cond->if_block, blok)->stmts, ast_cond->if_block));
    }

    for (size_t elif_idx = 0; elif_idx < elif_count; ++elif_idx) {
        const struct parse_node *const elif = ctrl->children[1 + elif_idx];
        const size_t elif_stmt_count = elif->nchildren - 4;

        aggr_error(&error, validate_expr(elif->children[1],
            &ast_cond->elif[elif_idx].expr, ast_node));

        if (unlikely(!(ast_cond->elif[elif_idx].block = ast_alloc_node(blok,
            elif_stmt_count * sizeof(struct ast_node *))))) {

            aggr_error(&error, NOMEM);
        } else {
            ast_set_node(ast_cond->elif[elif_idx].block, AST_AN_BLOK, ast_node, elif);
            ast_data(ast_cond->elif[elif_idx].block, blok)->stmt_count =
                elif_stmt_count;

            aggr_error(&error, validate_stmts(elif_stmt_count, 3, elif->children,
                ast_data(ast_cond->elif[elif_idx].block, blok)->stmts,
                    ast_cond->elif[elif_idx].block));
        }
    }

    if (else_idx) {
        const struct parse_node *const els = ctrl->children[else_idx];
        const size_t else_stmt_count = els->nchildren - 3;

        if (unlikely(!(ast_cond->else_block = ast_alloc_node(blok,
            else_stmt_count * sizeof(struct ast_node *))))) {

            aggr_error(&error, NOMEM);
        } else {
            ast_set_node(ast_cond->else_block, AST_AN_BLOK, ast_node, els);
            ast_data(ast_cond->else_block, blok)->stmt_count = else_stmt_count;

            aggr_error(&error, validate_stmts(else_stmt_count, 2, els->children,
                ast_data(ast_cond->else_block, blok)->stmts, ast_cond->else_block));
        }
    }

    return error;
}

static int validate_whil(const struct parse_node *const whil,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    const size_t stmt_count = whil->nchildren - 4;

    if (unlikely(!(*ast = ast_alloc_node(whil,
        stmt_count * sizeof(struct ast_node *))))) {

        return NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    struct ast_whil *const ast_whil = ast_data(*ast, whil);

    ast_set_node(ast_node, AST_AN_WHIL, parent, whil);
    ast_whil->stmt_count = stmt_count;

    int error = validate_expr(whil->children[1], &ast_whil->expr, *ast);

    aggr_error(&error, validate_stmts(stmt_count, 3,
        whil->children, ast_whil->stmts, ast_node));

    return error;
}

static int validate_dowh(const struct parse_node *const dowh,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    const size_t stmt_count = dowh->nchildren - 6;

    if (unlikely(!(*ast = ast_alloc_node(dowh,
        stmt_count * sizeof(struct ast_node *))))) {

        return NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    struct ast_dowh *const ast_dowh = ast_data(*ast, dowh);

    ast_set_node(ast_node, AST_AN_DOWH, parent, dowh);
    ast_dowh->stmt_count = stmt_count;

    int error = validate_stmts(stmt_count, 2,
        dowh->children, ast_dowh->stmts, ast_node);

    aggr_error(&error, validate_expr(dowh->children[dowh->nchildren - 2],
        &ast_dowh->expr, ast_node));

    return error;
}

static int validate_type(const struct parse_node *const stmt,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    const uint8_t expo = parse_node_tk(stmt->children[0]) == LEX_TK_EXPO;
    const struct parse_node *const child = stmt->children[expo + 1]->children[0];

    if (parse_node_nt(child) != PARSE_NT_Bexp) {
        return INVALID("bad type statement", child);
    }

    if (!expr_is_atom(LEX_TK_NAME, child->children[0])) {
        return INVALID("bad type name", child->children[0]);
    }

    if (parse_node_tk(child->children[1]) != LEX_TK_COLN) {
        return INVALID("expecting a colon after the type name", child);
    }

    if (unlikely(!(*ast = ast_alloc_node(type, 0)))) {
        return NOMEM;
    }

    struct type *const root_type = type_alloc;

    if (unlikely(!root_type)) {
        return NOMEM;
    }

    int error;

    if ((error = validate_typespec(child->children[2], root_type))) {
        return type_free(root_type), error;
    }

    struct lex_symbol *type_name = (struct lex_symbol *)
        child->children[0]->children[0]->children[0]->token;

    struct type_symtab_entry entry = {
        .type = root_type,
        .name = type_name,
    };

    if ((error = type_symtab_insert(&entry))) {
        type_free(root_type);

        return unlikely(error < 0) ? NOMEM :
            INVALID("redefinition of type", child->children[0]);
    }

    struct ast_node *const ast_node = *ast;
    struct ast_type *const ast_type = ast_data(*ast, type);

    ast_set_node(ast_node, AST_AN_TYPE, parent, stmt);
    ast_type->expo = expo;
    ast_type->name = type_name;
    ast_type->type = root_type;

    return AST_OK;
}

static int validate_retn(const struct parse_node *const stmt,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    const struct parse_node *const expr = stmt->nchildren == 3 ?
        stmt->children[1] : NULL;

    if (unlikely(!(*ast = ast_alloc_node(retn, 0)))) {
        return NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    struct ast_retn *const ast_retn = ast_data(*ast, retn);
    int error;

    if (expr && (error = validate_expr(expr, &ast_retn->expr, ast_node))) {
        return ast_destroy(ast_retn->expr), error;
    }

    ast_set_node(ast_node, AST_AN_RETN, parent, stmt);
    return AST_OK;
}

static int validate_wait(const struct parse_node *const stmt,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    enum {
        wait_expr_wfor_expr,
        wait_expr_wfor_expr_wnob,
        wait_expr_wunt_expr,
        wait_expr_wunt_expr_wnob,
        wait_expr,
        wait_expr_wnob,
    };

    char wait_variant;

    switch (stmt->nchildren) {
    case 3:
        wait_variant = wait_expr;
        break;

    case 4:
        wait_variant = wait_expr_wnob;
        break;

    case 5:
        wait_variant = parse_node_tk(stmt->children[2]) == LEX_TK_WFOR ?
            wait_expr_wfor_expr : wait_expr_wunt_expr;

        break;

    case 6:
        wait_variant = parse_node_tk(stmt->children[2]) == LEX_TK_WFOR ?
            wait_expr_wfor_expr_wnob : wait_expr_wunt_expr_wnob;

        break;

    default: assert(0), abort();
    }

    if (unlikely(!(*ast = ast_alloc_node(wait, 0)))) {
        return NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    struct ast_wait *const ast_wait = ast_data(*ast, wait);
    int error;

    if ((error = validate_expr(stmt->children[1], &ast_wait->wquaint, *ast))) {
        return ast_destroy(ast_wait->wquaint), error;
    }

    switch (wait_variant) {
    case wait_expr:
    case wait_expr_wnob:
        ast_wait->noblock = wait_variant == wait_expr_wnob;
        break;

    case wait_expr_wfor_expr:
    case wait_expr_wfor_expr_wnob: {
        const struct parse_node *expr = stmt->children[3];

        if (expr_is(PARSE_NT_Wexp, expr)) {
            ast_wait->units =
                parse_node_tk(expr->children[0]->children[1]) == LEX_TK_WSEC;

            expr = expr->children[0]->children[0];
        }

        if ((error = validate_expr(expr, &ast_wait->wfor, ast_node))) {
            return ast_destroy(ast_wait->wfor), error;
        }

        ast_wait->noblock = wait_variant == wait_expr_wfor_expr_wnob;
    } break;

    case wait_expr_wunt_expr:
    case wait_expr_wunt_expr_wnob: {
        const struct parse_node *expr = stmt->children[3];

        if ((error = validate_expr(expr, &ast_wait->wunt, ast_node))) {
            return ast_destroy(ast_wait->wunt), error;
        }

        ast_wait->noblock = wait_variant == wait_expr_wunt_expr_wnob;
    } break;

    default: assert(0), abort();
    }

    ast_set_node(ast_node, AST_AN_WAIT, parent, stmt);
    return AST_OK;
}

static int validate_expr(const struct parse_node *const expr,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    const parse_nt_t nt = parse_node_nt(expr);

    switch (nt) {
    case PARSE_NT_Expr: return validate_expr(expr->children[0], ast, parent);
    case PARSE_NT_Pexp: return validate_expr(expr->children[1], ast, parent);
    case PARSE_NT_Wexp: return INVALID("unexpected wexp", expr);
    case PARSE_NT_Bexp: *ast = ast_alloc_node(bexp, 0); break;
    case PARSE_NT_Uexp: *ast = ast_alloc_node(uexp, 0); break;
    case PARSE_NT_Fexp: *ast = ast_alloc_node(fexp, 0); break;
    case PARSE_NT_Xexp: *ast = ast_alloc_node(xexp, 0); break;
    case PARSE_NT_Aexp: *ast = ast_alloc_node(aexp, 0); break;
    case PARSE_NT_Texp: *ast = ast_alloc_node(texp, 0); break;
    case PARSE_NT_Atom: {
        switch (parse_node_tk(expr->children[0])) {
        case LEX_TK_NAME: *ast = ast_alloc_node(name, 0); break;
        case LEX_TK_NMBR: *ast = ast_alloc_node(nmbr, 0); break;
        case LEX_TK_STRL: *ast = ast_alloc_node(strl, 0); break;
        default: assert(0), abort();
        }
    } break;

    default: assert(0), abort();
    }

    if (unlikely(!*ast)) {
        return NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    ast_set_node(ast_node, AST_AN_VOID, parent, expr);
    int error;

    switch (nt) {
    case PARSE_NT_Bexp: {
        ast_node->an = AST_AN_BEXP;
        struct ast_bexp *const bexp = ast_data(*ast, bexp);
        bexp->op = parse_node_tk(expr->children[1]);

        if ((error = validate_expr(expr->children[0], &bexp->lhs, ast_node))) {
            return error;
        }

        if (bexp->op == LEX_TK_CAST || bexp->op == LEX_TK_COLN) {
            if (unlikely(!(bexp->cast = type_alloc))) {
                return NOMEM;
            }

            if ((error = validate_typespec(expr->children[2], bexp->cast))) {
                return type_free(bexp->cast), bexp->cast = NULL, error;
            }
        } else if ((error = validate_expr(expr->children[2], &bexp->rhs, ast_node))) {
            return error;
        }
    } break;

    case PARSE_NT_Uexp: {
        ast_node->an = AST_AN_UEXP;
        struct ast_uexp *const uexp = ast_data(*ast, uexp);
        uexp->op = parse_node_tk(expr->children[0]);

        if (uexp->op == LEX_TK_SZOF || uexp->op == LEX_TK_ALOF) {
            if (unlikely(!(uexp->typespec = type_alloc))) {
                return NOMEM;
            }

            if ((error = validate_typespec(expr->children[1], uexp->typespec))) {
                return type_free(uexp->typespec), uexp->typespec = NULL, error;
            }
        } else if ((error = validate_expr(expr->children[1], &uexp->rhs, ast_node))) {
            return error;
        }
    } break;

    case PARSE_NT_Fexp: {
        ast_node->an = AST_AN_FEXP;
        struct ast_fexp *const fexp = ast_data(*ast, fexp);

        if ((error = validate_expr(expr->children[0], &fexp->lhs, ast_node))) {
            return error;
        }

        if (expr->nchildren == 4 &&
            (error = validate_expr(expr->children[2], &fexp->rhs, ast_node))) {

            return error;
        }
    } break;

    case PARSE_NT_Xexp: {
        ast_node->an = AST_AN_XEXP;
        struct ast_xexp *const xexp = ast_data(*ast, xexp);
        xexp->op = parse_node_tk(expr->children[1]);

        if ((error = validate_expr(expr->children[0], &xexp->lhs, ast_node))) {
            return error;
        }
    } break;

    case PARSE_NT_Aexp: {
        ast_node->an = AST_AN_AEXP;
        struct ast_aexp *const aexp = ast_data(*ast, aexp);

        if ((error = validate_expr(expr->children[0], &aexp->base, ast_node))) {
            return error;
        }

        if ((error = validate_expr(expr->children[2], &aexp->off, ast_node))) {
            return error;
        }
    } break;

    case PARSE_NT_Texp: {
        ast_node->an = AST_AN_TEXP;
        struct ast_texp *const texp = ast_data(*ast, texp);

        if ((error = validate_expr(expr->children[0], &texp->cond, ast_node))) {
            return error;
        }

        if ((error = validate_expr(expr->children[2], &texp->tval, ast_node))) {
            return error;
        }

        if ((error = validate_expr(expr->children[4], &texp->fval, ast_node))) {
            return error;
        }
    } break;

    case PARSE_NT_Atom: {
        switch (parse_node_tk(expr->children[0])) {
        case LEX_TK_NAME:
            ast_node->an = AST_AN_NAME;
            break;

        case LEX_TK_NMBR: {
            ast_node->an = AST_AN_NMBR;
            struct ast_nmbr *const nmbr = ast_data(*ast, nmbr);
            nmbr->value = nmbr_to_uint(expr->children[0]);
        } break;

        case LEX_TK_STRL: {
            ast_node->an = AST_AN_STRL;
            struct ast_strl *const strl = ast_data(*ast, strl);

            strl->str = (struct lex_symbol) {
                .beg = ast_node->ltok->beg + 1,
                .end = ast_node->rtok->end - 1,
            };
        } break;

        default: assert(0), abort();
        }
    } break;

    default: assert(0), abort();
    }

    return AST_OK;
}

static int validate_decl_or_expr(const struct parse_node *const stmt,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    size_t qual_count;
    uint8_t cons, expo, stat;

    const struct parse_node *const child = count_decl_qualifiers(
        stmt->children[0], &qual_count, &cons, &expo, &stat);

    if (!child) {
        return AST_INVALID;
    }

    const struct parse_node *expr = child;
    size_t name_count = 0;
    struct lex_symbol **names = NULL;
    struct type *type = NULL;
    const struct parse_node *init_expr = NULL;
    int error;

    for (;;) {
        if (!expr_is(PARSE_NT_Bexp, expr)) {
            (void) (qual_count && INVALID("expecting a bexp", expr));
            goto out;
        }

        const struct parse_node *const bexp = expr->children[0];
        const struct parse_node *left = bexp->children[0];
        const struct parse_node *op = bexp->children[1];
        const struct parse_node *right = bexp->children[2];

        if (parse_node_tk(op) == LEX_TK_ASSN) {
            if (!expr_is(PARSE_NT_Bexp, left)) {
                (void) (qual_count && INVALID("expecting a bexp", left));
                goto out;
            }

            op = left->children[0]->children[1];

            if (parse_node_tk(op) != LEX_TK_COLN) {
                (void) (qual_count && INVALID("expecting a colon", op));
                goto out;
            }

            init_expr = right;
            right = left->children[0]->children[2];
            left = left->children[0]->children[0];
        }

        if (!expr_is_atom(LEX_TK_NAME, left)) {
            (void) (qual_count && INVALID("expecting a name", left));
            goto out;
        }

        struct lex_symbol **const tmp = realloc(names,
            ++name_count * sizeof(struct lex_symbol *));

        if (unlikely(!tmp)) {
            return free(names), NOMEM;
        }

        (names = tmp)[name_count - 1] = (struct lex_symbol *)
            left->children[0]->children[0]->token;

        if (parse_node_tk(op) != LEX_TK_COMA) {
            if (parse_node_tk(op) != LEX_TK_COLN) {
                (void) (qual_count && INVALID("expecting a colon", op));
                goto out;
            }

            if (unlikely(!(type = type_alloc))) {
                return free(names), NOMEM;
            }

            if ((error = validate_typespec(right, type))) {
                return free(names), type_free(type), error;
            }

            break;
        }

        expr = right;
    }

    for (size_t i = 0; i < name_count; ++i) {
        for (size_t j = i + 1; j < name_count; ++j) {
            if (unlikely(lex_symbols_equal(names[i], names[j]))) {
                const struct lex_token *const token = (struct lex_token *) names[j];

                return free(names), type_free(type),
                    INVALID_TK("duplicate name in declaration", token);
            }
        }
    }

    if (unlikely(!(*ast = ast_alloc_node(decl, 0)))) {
        return free(names), type_free(type), NOMEM;
    }

    struct ast_node *const ast_node = *ast;
    struct ast_decl *const ast_decl = ast_data(*ast, decl);

    if (init_expr &&
        (error = validate_expr(init_expr, &ast_decl->init_expr, ast_node))) {

        return free(names), type_free(type),
            ast_destroy(ast_decl->init_expr), error;
    }

    ast_set_node(ast_node, AST_AN_DECL, parent, stmt);
    ast_decl->cons = cons;
    ast_decl->expo = expo;
    ast_decl->stat = stat;
    ast_decl->name_count = name_count;
    ast_decl->names = names;
    ast_decl->type = type;

    return AST_OK;

out:
    free(names);
    return qual_count ? TYPE_INVALID : validate_expr(child, ast, parent);
}

static int validate_stmt(const struct parse_node *const stmt,
    struct ast_node **const ast, const struct ast_node *const parent)
{
    const ctx_t ctx = top_context();

    if (parse_node_is_tk(stmt->children[0])) {
        switch (parse_node_tk(stmt->children[0])) {
        case LEX_TK_EXPO:
        case LEX_TK_TYPE: {
            if (ctx != CTX_UNIT) {
                return INVALID("type statement not in unit context", stmt);
            }

            return validate_type(stmt, ast, parent);
        }

        case LEX_TK_WAIT: {
            if (ctx == CTX_UNIT) {
                return INVALID("wait statement in unit context", stmt);
            }

            return validate_wait(stmt, ast, parent);
        }

        case LEX_TK_RETN: {
            if (ctx == CTX_UNIT) {
                return INVALID("return statement in unit context", stmt);
            }

            return validate_retn(stmt, ast, parent);
        }

        case LEX_TK_LBRA: {
            if (ctx == CTX_UNIT) {
                return INVALID("wait label in unit context", stmt);
            }

            return validate_wlab(stmt, ast, parent);
        }

        case LEX_TK_LBRC:
        case LEX_TK_NOIN: {
            if (ctx == CTX_UNIT) {
                return INVALID("block in unit context", stmt);
            }

            if (ctx != CTX_BLOCK) {
                push_context(CTX_BLOCK);
            }

            const int error = validate_blok(stmt, ast, parent);

            if (ctx != CTX_BLOCK) {
                pop_context();
            }

            return error;
        }

        default: assert(0), abort();
        }
    } else {
        switch (parse_node_nt(stmt->children[0])) {
        case PARSE_NT_Qual:
        case PARSE_NT_Expr: {
            return validate_decl_or_expr(stmt, ast, parent);
        }

        case PARSE_NT_Ctrl: {
            if (ctx == CTX_UNIT) {
                return INVALID("control-flow statement in unit context", stmt);
            }

            if (ctx != CTX_BLOCK) {
                push_context(CTX_BLOCK);
            }

            const int error = validate_ctrl(stmt->children[0], ast, parent);

            if (ctx != CTX_BLOCK) {
                pop_context();
            }

            return error;
        }

        case PARSE_NT_Func: {
            if (ctx != CTX_UNIT) {
                return INVALID("function not in unit context", stmt);
            }

            push_context(CTX_FUNC);
            const int error = validate_func(stmt->children[0], ast, parent);
            pop_context();
            return error;
        }

        default: assert(0), abort();
        }
    }
}

int ast_build(const struct parse_node *const unit, struct ast_node **const root)
{
    const size_t stmt_count = unit->nchildren - 2;

    if (unlikely(!(*root = ast_alloc_node(unit,
        stmt_count * sizeof(struct ast_node *))))) {

        return AST_NOMEM;
    }

    struct ast_node *const ast_node = *root;
    struct ast_unit *const ast_unit = ast_data(*root, unit);

    ast_set_node(ast_node, AST_AN_UNIT, NULL, unit);
    ast_unit->stmt_count = stmt_count;

    assert(ctx_stack.size == 0);
    push_context(CTX_UNIT);

    const int error = validate_stmts(stmt_count, 1, unit->children,
        ast_unit->stmts, ast_node);

    pop_context();
    assert(ctx_stack.size == 0);
    return error;
}

void ast_destroy(struct ast_node *const ast)
{
    if (!ast) {
        return;
    }

    switch (ast->an) {
    case AST_AN_VOID: {
    } break;

    case AST_AN_UNIT: {
        const struct ast_unit *const unit = ast_data(ast, unit);

        for (size_t idx = 0; idx < unit->stmt_count; ++idx) {
            ast_destroy(unit->stmts[idx]);
        }

        free(unit->scope);
    } break;

    case AST_AN_TYPE: {
        const struct ast_type *const type = ast_data(ast, type);
        (void) type;
    } break;

    case AST_AN_DECL: {
        const struct ast_decl *const decl = ast_data(ast, decl);
        free(decl->names);
        type_free(decl->type);
        ast_destroy(decl->init_expr);
    } break;

    case AST_AN_FUNC: {
        const struct ast_func *const func = ast_data(ast, func);
        free(func->wlabs);
        type_free(func->rettype);

        for (size_t idx = 0; idx < func->param_count; ++idx) {
            type_free(func->params[idx].type);
        }

        free(func->params);

        for (size_t idx = 0; idx < func->stmt_count; ++idx) {
            ast_destroy(func->stmts[idx]);
        }

        free(func->scope);
    } break;

    case AST_AN_BLOK:
    case AST_AN_NOIN: {
        const struct ast_blok *const blok = ast_data(ast, blok);

        for (size_t idx = 0; idx < blok->stmt_count; ++idx) {
            ast_destroy(blok->stmts[idx]);
        }

        free(blok->scope);
    } break;

    case AST_AN_COND: {
        const struct ast_cond *const cond = ast_data(ast, cond);
        ast_destroy(cond->if_expr);
        ast_destroy(cond->if_block);
        ast_destroy(cond->else_block);

        for (size_t idx = 0; idx < cond->elif_count; ++idx) {
            ast_destroy(cond->elif[idx].expr);
            ast_destroy(cond->elif[idx].block);
        }
    } break;

    case AST_AN_WHIL: {
        const struct ast_whil *const whil = ast_data(ast, whil);
        ast_destroy(whil->expr);

        for (size_t idx = 0; idx < whil->stmt_count; ++idx) {
            ast_destroy(whil->stmts[idx]);
        }

        free(whil->scope);
    } break;

    case AST_AN_DOWH: {
        const struct ast_dowh *const dowh = ast_data(ast, dowh);
        ast_destroy(dowh->expr);

        for (size_t idx = 0; idx < dowh->stmt_count; ++idx) {
            ast_destroy(dowh->stmts[idx]);
        }

        free(dowh->scope);
    } break;

    case AST_AN_RETN: {
        const struct ast_retn *const retn = ast_data(ast, retn);
        ast_destroy(retn->expr);
    } break;

    case AST_AN_WAIT: {
        const struct ast_wait *const wait = ast_data(ast, wait);
        ast_destroy(wait->wquaint);
        ast_destroy(wait->wfor);
        ast_destroy(wait->wunt);
    } break;

    case AST_AN_WLAB: {
        const struct ast_wlab *const wlab = ast_data(ast, wlab);
        (void) wlab;
    } break;

    case AST_AN_BEXP: {
        const struct ast_bexp *const bexp = ast_data(ast, bexp);
        ast_destroy(bexp->lhs);

        if (bexp->op == LEX_TK_CAST || bexp->op == LEX_TK_COLN) {
            type_free(bexp->cast);
        } else {
            ast_destroy(bexp->rhs);
        }

        type_free(bexp->type);
    } break;

    case AST_AN_UEXP: {
        const struct ast_uexp *const uexp = ast_data(ast, uexp);

        if (uexp->op == LEX_TK_SZOF || uexp->op == LEX_TK_ALOF) {
            type_free(uexp->typespec);
        } else {
            ast_destroy(uexp->rhs);
        }

        type_free(uexp->type);
    } break;

    case AST_AN_FEXP: {
        const struct ast_fexp *const fexp = ast_data(ast, fexp);
        ast_destroy(fexp->lhs);
        ast_destroy(fexp->rhs);
        type_free(fexp->type);
    } break;

    case AST_AN_XEXP: {
        const struct ast_xexp *const xexp = ast_data(ast, xexp);
        ast_destroy(xexp->lhs);
        type_free(xexp->type);
    } break;

    case AST_AN_AEXP: {
        const struct ast_aexp *const aexp = ast_data(ast, aexp);
        ast_destroy(aexp->base);
        ast_destroy(aexp->off);
        type_free(aexp->type);
    } break;

    case AST_AN_TEXP: {
        const struct ast_texp *const texp = ast_data(ast, texp);
        ast_destroy(texp->cond);
        ast_destroy(texp->tval);
        ast_destroy(texp->fval);
        type_free(texp->type);
    } break;

    case AST_AN_NAME: {
        const struct ast_name *const name = ast_data(ast, name);
        type_free(name->type);
    } break;

    case AST_AN_NMBR:
    case AST_AN_STRL:
        break;

    default: assert(0), abort();
    }

    free(ast);
}

void ast_print(FILE *const out, const struct ast_node *const ast, const int level)
{
    #define print(...) \
        fprintf(out, __VA_ARGS__)

    #define print_scope(s) ({ \
        if (!(s)) { \
            print_indent, print(YELLOW("scop ") CYAN("null\n")); \
        } else { \
            print_indent, print(YELLOW("scop ") WHITE("(%zu): "), (s)->objcount); \
            \
            for (size_t idx = 0; idx < (s)->objcount; ++idx) { \
                const struct scope_obj *const so = &(s)->objs[idx]; \
                lex_print_symbol(stdout, CYAN("%.*s"), so->name); \
                print(WHITE(":%u"), so->obj); \
                print(idx == (s)->objcount - 1 ? ";" : ", "); \
            } \
            \
            print("\n"); \
        } \
    })

    #define print_indent ({ \
        for (int indent = 0; indent < level; ++indent) { \
            print("|   "); \
        } \
    })

    #define print_end \
        print_indent, print(YELLOW("●\n"))

    print_indent;

    if (!ast) {
        print(CYAN("null\n"));
        return;
    }

    switch (ast->an) {
    case AST_AN_VOID: {
        print(RED("void\n"));
    } break;

    case AST_AN_UNIT: {
        const struct ast_unit *const unit = ast_data(ast, unit);
        print(YELLOW("unit\n"));
        print_scope(unit->scope);

        for (size_t idx = 0; idx < unit->stmt_count; ++idx) {
            ast_print(out, unit->stmts[idx], level + 1);
        }

        print_end;
    } break;

    case AST_AN_TYPE: {
        const struct ast_type *const type = ast_data(ast, type);

        if (type->expo) {
            print(GREEN("exposed "));
        }

        print(YELLOW("type "));
        lex_print_symbol(out, "%.*s: ", type->name);
        type_print(stdout, type->type);
        print("\n");
    } break;

    case AST_AN_DECL: {
        const struct ast_decl *const decl = ast_data(ast, decl);
        print(YELLOW("decl "));

        if (decl->cons) {
            print(GREEN("const "));
        }

        if (decl->expo) {
            print(GREEN("exposed "));
        }

        if (decl->stat) {
            print(GREEN("static "));
        }

        for (size_t idx = 0; idx < decl->name_count; ++idx) {
            lex_print_symbol(out, WHITE("%.*s"), decl->names[idx]);
            print(idx == decl->name_count - 1 ? WHITE(": ") : WHITE(", "));
        }

        type_print(out, decl->type);

        if (decl->init_expr) {
            print(WHITE(" =\n"));
            ast_print(out, decl->init_expr, level + 1);
            print_end;
        } else {
            print("\n");
        }
    } break;

    case AST_AN_FUNC: {
        const struct ast_func *const func = ast_data(ast, func);
        print(YELLOW("func "));

        if (func->expo) {
            print(GREEN("exposed "));
        }

        lex_print_symbol(out, WHITE("%.*s"), func->name);

        if (func->params) {
            print(WHITE("("));

            for (size_t idx = 0; idx < func->param_count; ++idx) {
                lex_print_symbol(out, WHITE("%.*s: "), func->params[idx].name);
                type_print(out, func->params[idx].type);

                if (idx != func->param_count - 1) {
                    print(WHITE(", "));
                }
            }

            print(WHITE(")"));
        }

        if (func->rettype) {
            print(WHITE(": "));
            type_print(out, func->rettype);
        }

        print("\n");
        print_scope(func->scope);

        for (size_t idx = 0; idx < func->stmt_count; ++idx) {
            ast_print(out, func->stmts[idx], level + 1);
        }

        print_end;
    } break;

    case AST_AN_BLOK:
    case AST_AN_NOIN: {
        const struct ast_blok *const blok = ast_data(ast, blok);
        print(ast->an == AST_AN_BLOK ? YELLOW("blok\n") : YELLOW("noin\n"));
        print_scope(blok->scope);

        for (size_t idx = 0; idx < blok->stmt_count; ++idx) {
            ast_print(out, blok->stmts[idx], level + 1);
        }

        print_end;
    } break;

    case AST_AN_COND: {
        const struct ast_cond *const cond = ast_data(ast, cond);
        print(YELLOW("cond\n"));
        ast_print(out, cond->if_expr, level + 1);
        print_indent, print(YELLOW("blok") WHITE(" (if)\n"));
        print_scope(ast_data(cond->if_block, blok)->scope);

        for (size_t idx = 0; idx < ast_data(cond->if_block, blok)->
            stmt_count; ++idx) {

            ast_print(out, ast_data(cond->if_block, blok)->
                stmts[idx], level + 1);
        }

        for (size_t elif_idx = 0; elif_idx < cond->elif_count; ++elif_idx) {
            print_indent, print(YELLOW("elif\n"));
            ast_print(out, cond->elif[elif_idx].expr, level + 1);
            print_indent, print(YELLOW("blok") WHITE(" (elif)\n"));
            print_scope(ast_data(cond->elif[elif_idx].block, blok)->scope);

            for (size_t idx = 0; idx < ast_data(
                cond->elif[elif_idx].block, blok)->stmt_count; ++idx) {

                ast_print(out, ast_data(cond->elif[elif_idx].block, blok)->
                    stmts[idx], level + 1);
            }
        }

        if (cond->else_block) {
            print_indent, print(YELLOW("else\n"));
            print_scope(ast_data(cond->else_block, blok)->scope);

            for (size_t idx = 0; idx < ast_data(
                cond->else_block, blok)->stmt_count; ++idx) {

                ast_print(out, ast_data(cond->else_block, blok)->
                    stmts[idx], level + 1);
            }
        }

        print_end;
    } break;

    case AST_AN_WHIL: {
        const struct ast_whil *const whil = ast_data(ast, whil);
        print(YELLOW("whil\n"));
        ast_print(out, whil->expr, level + 1);
        print_indent, print(YELLOW("blok") WHITE(" (while)\n"));
        print_scope(whil->scope);

        for (size_t idx = 0; idx < whil->stmt_count; ++idx) {
            ast_print(out, whil->stmts[idx], level + 1);
        }

        print_end;
    } break;

    case AST_AN_DOWH: {
        const struct ast_dowh *const dowh = ast_data(ast, dowh);
        print(YELLOW("blok") WHITE(" (do-while)\n"));
        print_scope(dowh->scope);

        for (size_t idx = 0; idx < dowh->stmt_count; ++idx) {
            ast_print(out, dowh->stmts[idx], level + 1);
        }

        print_indent, print(YELLOW("dowh\n"));
        ast_print(out, dowh->expr, level + 1);
        print_end;
    } break;

    case AST_AN_RETN: {
        const struct ast_retn *const retn = ast_data(ast, retn);
        print(YELLOW("retn\n"));

        if (retn->expr) {
            ast_print(out, retn->expr, level + 1);
            print_end;
        }
    } break;

    case AST_AN_WAIT: {
        const struct ast_wait *const wait = ast_data(ast, wait);
        print(YELLOW("wait ") WHITE("%s\n"), wait->noblock ? "noblock" : "");
        ast_print(out, wait->wquaint, level + 1);

        if (wait->wfor) {
            print_indent, print(YELLOW("wfor ") WHITE("%s\n"), wait->units ?
                "sec" : "msec");

            ast_print(out, wait->wfor, level + 1);
        } else if (wait->wunt) {
            print_indent, print(YELLOW("wunt\n"));
            ast_print(out, wait->wunt, level + 1);
        }

        print_end;
    } break;

    case AST_AN_WLAB: {
        const struct ast_wlab *const wlab = ast_data(ast, wlab);
        print(YELLOW("wlab") " [");
        lex_print_symbol(out, WHITE("%.*s"), wlab->name);
        print("]\n");
    } break;

    case AST_AN_BEXP: {
        const struct ast_bexp *const bexp = ast_data(ast, bexp);
        print(YELLOW("bexp "));

        switch (bexp->op) {
        case LEX_TK_ASSN: print(WHITE("="  )); break;
        case LEX_TK_ASPL: print(WHITE("+=" )); break;
        case LEX_TK_ASMI: print(WHITE("-=" )); break;
        case LEX_TK_ASMU: print(WHITE("*=" )); break;
        case LEX_TK_ASDI: print(WHITE("/=" )); break;
        case LEX_TK_ASMO: print(WHITE("%%=")); break;
        case LEX_TK_ASLS: print(WHITE("<<=")); break;
        case LEX_TK_ASRS: print(WHITE(">>=")); break;
        case LEX_TK_ASAN: print(WHITE("&=" )); break;
        case LEX_TK_ASXO: print(WHITE("^=" )); break;
        case LEX_TK_ASOR: print(WHITE("|=" )); break;
        case LEX_TK_COLN: print(WHITE(":"  )); break;
        case LEX_TK_SCOP: print(WHITE("::" )); break;
        case LEX_TK_ATSI: print(WHITE("@"  )); break;
        case LEX_TK_MEMB: print(WHITE("."  )); break;
        case LEX_TK_AROW: print(WHITE("->" )); break;
        case LEX_TK_EQUL: print(WHITE("==" )); break;
        case LEX_TK_NEQL: print(WHITE("!=" )); break;
        case LEX_TK_LTHN: print(WHITE("<"  )); break;
        case LEX_TK_GTHN: print(WHITE(">"  )); break;
        case LEX_TK_LTEQ: print(WHITE("<=" )); break;
        case LEX_TK_GTEQ: print(WHITE(">=" )); break;
        case LEX_TK_CONJ: print(WHITE("&&" )); break;
        case LEX_TK_DISJ: print(WHITE("||" )); break;
        case LEX_TK_PLUS: print(WHITE("+"  )); break;
        case LEX_TK_MINS: print(WHITE("-"  )); break;
        case LEX_TK_MULT: print(WHITE("*"  )); break;
        case LEX_TK_DIVI: print(WHITE("/"  )); break;
        case LEX_TK_MODU: print(WHITE("%%" )); break;
        case LEX_TK_LSHF: print(WHITE("<<" )); break;
        case LEX_TK_RSHF: print(WHITE(">>" )); break;
        case LEX_TK_AMPS: print(WHITE("&"  )); break;
        case LEX_TK_CARE: print(WHITE("^"  )); break;
        case LEX_TK_PIPE: print(WHITE("|"  )); break;
        case LEX_TK_COMA: print(WHITE(","  )); break;
        case LEX_TK_CAST: print(WHITE("as" )); break;
        default: assert(0), abort();
        }

        if (bexp->op == LEX_TK_CAST || bexp->op == LEX_TK_COLN) {
            print(" ");

            if (bexp->cast) {
                type_print(out, bexp->cast);
            } else {
                print(CYAN("null"));
            }

            print("\n");
            ast_print(out, bexp->lhs, level + 1);
        } else {
            print(" (");

            if (bexp->type) {
                type_print(out, bexp->type);
            } else {
                print(CYAN("null"));
            }

            print(")\n");
            ast_print(out, bexp->lhs, level + 1);
            ast_print(out, bexp->rhs, level + 1);
        }

        print_end;
    } break;

    case AST_AN_UEXP: {
        const struct ast_uexp *const uexp = ast_data(ast, uexp);
        print(YELLOW("uexp "));

        switch (uexp->op) {
        case LEX_TK_PLUS: print(WHITE("+"      )); break;
        case LEX_TK_MINS: print(WHITE("-"      )); break;
        case LEX_TK_EXCL: print(WHITE("!"      )); break;
        case LEX_TK_TILD: print(WHITE("~"      )); break;
        case LEX_TK_MULT: print(WHITE("*"      )); break;
        case LEX_TK_AMPS: print(WHITE("&"      )); break;
        case LEX_TK_CARE: print(WHITE("^"      )); break;
        case LEX_TK_INCR: print(WHITE("++"     )); break;
        case LEX_TK_DECR: print(WHITE("--"     )); break;
        case LEX_TK_SZOF: print(WHITE("sizeof" )); break;
        case LEX_TK_ALOF: print(WHITE("alignof")); break;
        default: assert(0), abort();
        }

        print(" (");

        if (uexp->type) {
            type_print(out, uexp->type);
        } else {
            print(CYAN("null"));
        }

        print(")");

        if (uexp->op != LEX_TK_SZOF && uexp->op != LEX_TK_ALOF) {
            print("\n");
            ast_print(out, uexp->rhs, level + 1);
        } else {
            print(" ");

            if (uexp->typespec) {
                type_print(out, uexp->typespec);
            } else {
                print(CYAN("null"));
            }

            print("\n");
        }

        print_end;
    } break;

    case AST_AN_FEXP: {
        const struct ast_fexp *const fexp = ast_data(ast, fexp);
        print(YELLOW("fexp "));

        print("(");

        if (fexp->type) {
            type_print(out, fexp->type);
        } else {
            print(CYAN("null"));
        }

        print(")\n");

        ast_print(out, fexp->lhs, level + 1);
        ast_print(out, fexp->rhs, level + 1);
        print_end;
    } break;

    case AST_AN_XEXP: {
        const struct ast_xexp *const xexp = ast_data(ast, xexp);
        print(YELLOW("xexp "));

        switch (xexp->op) {
        case LEX_TK_INCR: print(WHITE("++")); break;
        case LEX_TK_DECR: print(WHITE("--")); break;
        default: assert(0), abort();
        }

        print(" (");

        if (xexp->type) {
            type_print(out, xexp->type);
        } else {
            print(CYAN("null"));
        }

        print(")\n");
        ast_print(out, xexp->lhs, level + 1);
        print_end;
    } break;

    case AST_AN_AEXP: {
        const struct ast_aexp *const aexp = ast_data(ast, aexp);
        print(YELLOW("aexp "));
        print("(");

        if (aexp->type) {
            type_print(out, aexp->type);
        } else {
            print(CYAN("null"));
        }

        print(")\n");
        ast_print(out, aexp->base, level + 1);
        ast_print(out, aexp->off, level + 1);
        print_end;
    } break;

    case AST_AN_TEXP: {
        const struct ast_texp *const texp = ast_data(ast, texp);
        print(YELLOW("texp "));
        print("(");

        if (texp->type) {
            type_print(out, texp->type);
        } else {
            print(CYAN("null"));
        }

        print(")\n");
        ast_print(out, texp->cond, level + 1);
        ast_print(out, texp->tval, level + 1);
        ast_print(out, texp->fval, level + 1);
        print_end;
    } break;

    case AST_AN_NAME: {
        const struct ast_name *const name = ast_data(ast, name);
        print(YELLOW("name "));
        lex_print_symbol(out, WHITE("%.*s: "), (struct lex_symbol *) ast->ltok);

        if (name->type) {
            type_print(out, name->type);
        } else {
            print(CYAN("null"));
        }

        print("\n");
    } break;

    case AST_AN_NMBR: {
        const struct ast_nmbr *const nmbr = ast_data(ast, nmbr);
        print(YELLOW("nmbr "));
        lex_print_symbol(out, WHITE("%.*s: "), (struct lex_symbol *) ast->ltok);

        if (nmbr->type) {
            type_print(out, nmbr->type);
        } else {
            print(CYAN("null"));
        }

        print("\n");
    } break;

    case AST_AN_STRL: {
        const struct ast_strl *const strl = ast_data(ast, strl);
        print(YELLOW("strl "));
        lex_print_symbol(out, WHITE("%.*s: "), (struct lex_symbol *) ast->ltok);

        if (strl->type) {
            type_print(out, strl->type);
        } else {
            print(CYAN("null"));
        }

        print("\n");
    } break;

    default: assert(0), abort();
    }

    #undef print
    #undef print_scope
    #undef print_indent
    #undef print_end
}
