#pragma once

#include "lex.h"
struct parse_node;
struct scope;

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>

enum {
    AST_AN_VOID = 0,

    AST_AN_UNIT, // translation unit

    AST_AN_TYPE, // type declaration
    AST_AN_DECL, // variable declaration

    AST_AN_FUNC, // function
    AST_AN_BLOK, // lexical block
    AST_AN_NOIN, // noint block
    AST_AN_COND, // if-elif-else chain
    AST_AN_WHIL, // while statement
    AST_AN_DOWH, // do-while statement

    AST_AN_RETN, // return statement
    AST_AN_WAIT, // wait statement
    AST_AN_WLAB, // wait label

    AST_AN_BEXP, // binary expression: x OP y
    AST_AN_UEXP, // unary expression: OP x
    AST_AN_FEXP, // function-call expression: x(y)
    AST_AN_XEXP, // postfix expression: x OP
    AST_AN_AEXP, // array-subscript expression: x[y]
    AST_AN_TEXP, // ternary expression: x ? y : z

    AST_AN_NAME, // naked identifier
    AST_AN_NMBR, // unsigned integer number literal
    AST_AN_STRL, // string literal

    AST_AN_COUNT,
};

static_assert(AST_AN_COUNT - 1 <= UINT8_MAX, "");
typedef uint8_t ast_an_t;

#define ast_data(ast, _type) \
    ((struct ast_##_type *const) (ast)->data)

struct ast_node {
    ast_an_t an;
    const struct ast_node *parent;
    const struct lex_token *ltok, *rtok;
    uint8_t data[] __attribute__((aligned(sizeof(void *))));
};

#define STMT_BLOCK \
    struct scope *scope; \
    size_t stmt_count; \
    struct ast_node *stmts[]

struct ast_unit {
    STMT_BLOCK;
};

struct ast_type {
    uint8_t expo: 1;
    const struct lex_symbol *name;
    const struct type *type;
};

struct ast_decl {
    uint8_t cons: 1, expo: 1, stat: 1;
    size_t name_count;
    struct lex_symbol **names;
    struct type *type;
    struct ast_node *init_expr;
};

struct ast_func {
    uint8_t expo: 1;
    const struct lex_symbol *name;
    size_t param_count;
    struct type_nt_pair *params;
    struct type *rettype;
    size_t wlab_count;

    struct {
        const struct lex_symbol *name;
        uint64_t id;
    } *wlabs;

    STMT_BLOCK;
};

struct ast_blok {
    STMT_BLOCK;
};

struct ast_cond {
    struct ast_node *if_expr, *if_block;
    struct ast_node *else_block;
    size_t elif_count;

    struct {
        struct ast_node *expr, *block;
    } elif[];
};

struct ast_whil {
    struct ast_node *expr;
    STMT_BLOCK;
};

struct ast_dowh {
    struct ast_node *expr;
    STMT_BLOCK;
};

#undef STMT_BLOCK

struct ast_retn {
    struct ast_node *expr;
};

struct ast_wait {
    struct ast_node *wquaint, *wfor, *wunt;
    uint8_t noblock: 1, units: 1;

    /* wunt != NULL */
    const struct ast_func *func;
    size_t wlab_idx;
};

struct ast_wlab {
    const struct lex_symbol *name;
    uintptr_t func;
    uint64_t id;
};

struct ast_bexp {
    lex_tk_t op;
    struct ast_node *lhs;

    union {
        /* op != LEX_TK_CAST && op != LEX_TK_COLN */
        struct ast_node *rhs;

        /* op == LEX_TK_CAST || op == LEX_TK_COLN */
        struct type *cast;
    };

    union {
        /* op == LEX_TK_MEMB || op == LEX_TK_AROW */
        size_t member_idx;

        /* op == LEX_TK_ATSI */
        struct {
            const struct ast_func *func;
            size_t wlab_idx;
        };
    };

    struct type *type;
};

struct ast_uexp {
    lex_tk_t op;

    union {
        /* op != LEX_TK_SZOF && op != LEX_TK_ALOF */
        struct ast_node *rhs;

        /* op == LEX_TK_SZOF || op == LEX_TK_ALOF */
        struct type *typespec;
    };

    struct type *type;
};

struct ast_fexp {
    struct ast_node *lhs, *rhs;
    size_t arg_count;
    struct type *type;
};

struct ast_xexp {
    lex_tk_t op;
    struct ast_node *lhs;
    struct type *type;
};

struct ast_aexp {
    struct ast_node *base, *off;
    struct type *type;
};

struct ast_texp {
    struct ast_node *cond, *tval, *fval;
    struct type *type;
};

struct ast_name {
    const struct scope_obj *scoped;
    struct type *type;
};

struct ast_nmbr {
    uint64_t value;
    const struct type *type;
};

struct ast_strl {
    struct lex_symbol str;
    const struct type *type;
};

int ast_build(const struct parse_node *, struct ast_node **);

enum {
    AST_OK = 0,
    AST_NOMEM,
    AST_INVALID,
};

void ast_destroy(const struct ast_node *);
void ast_print(FILE *, const struct ast_node *, int);
