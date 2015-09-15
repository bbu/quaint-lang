#pragma once

struct lex_token;

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

enum {
    PARSE_NT_Unit,
    PARSE_NT_Stmt,
    PARSE_NT_Ctrl,
    PARSE_NT_Cond,
    PARSE_NT_Elif,
    PARSE_NT_Else,
    PARSE_NT_Dowh,
    PARSE_NT_Whil,
    PARSE_NT_Func,
    PARSE_NT_Qual,
    PARSE_NT_Atom,
    PARSE_NT_Expr,
    PARSE_NT_Fexp,
    PARSE_NT_Pexp,
    PARSE_NT_Texp,
    PARSE_NT_Bexp,
    PARSE_NT_Uexp,
    PARSE_NT_Xexp,
    PARSE_NT_Wexp,
    PARSE_NT_Aexp,
    PARSE_NT_COUNT
};

static_assert(PARSE_NT_COUNT - 1 <= UINT8_MAX, "");
typedef uint8_t parse_nt_t;

struct parse_node {
    size_t nchildren;

    union {
        /* nchildren == 0 */
        const struct lex_token *token;

        /* nchildren != 0 */
        struct {
            parse_nt_t nt;
            struct parse_node **children;
        };
    };
};

#define parse_node_is_tk(node) ((node)->nchildren == 0)
#define parse_node_tk(node)    ((node)->token->tk)
#define parse_node_is_nt(node) ((node)->nchildren != 0)
#define parse_node_nt(node)    ((node)->nt)

struct parse_node parse(const struct lex_token *, size_t);

enum {
    PARSE_OK = 0,
    PARSE_NOMEM,
    PARSE_REJECT,
};

#define parse_error(root) ({ \
    struct parse_node root_once = (root); \
    root_once.nchildren ? PARSE_OK : root_once.token->tk; \
})

void parse_tree_destroy(struct parse_node);
void parse_node_ltok_rtok(const struct parse_node *,
    const struct lex_token **, const struct lex_token **);
