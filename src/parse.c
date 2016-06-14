#include "parse.h"

#include "lex.h"

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define RULE_RHS_LAST 7
#define GRAMMAR_SIZE countof(grammar)

#define skip_token(t) ({ \
    const lex_tk_t t_once = (t); \
    t_once == LEX_TK_WSPC || t_once == LEX_TK_LCOM || t_once == LEX_TK_BCOM; \
})

static struct {
    size_t size, allocated;
    struct parse_node *nodes;
} stack;

struct term {
    /* a rule RHS term is either a terminal token or a non-terminal */
    union {
        const lex_tk_t tk;
        const parse_nt_t nt;
    };

    /* indicates which field of the above union to use */
    const uint8_t is_tk: 1;

    /* indicates that the non-terminal can be matched multiple times */
    const uint8_t is_mt: 1;
};

struct rule {
    /* left-hand side of rule */
    const parse_nt_t lhs;

    /* right-hand side of rule */
    const struct term rhs[RULE_RHS_LAST + 1];
};

#define n(_nt) { .nt = PARSE_NT_##_nt, .is_tk = 0, .is_mt = 0 }
#define m(_nt) { .nt = PARSE_NT_##_nt, .is_tk = 0, .is_mt = 1 }
#define t(_tm) { .tk = LEX_TK_##_tm,   .is_tk = 1, .is_mt = 0 }
#define no     { .tk = LEX_TK_COUNT,   .is_tk = 1, .is_mt = 0 }

#define r1(_lhs, t1) \
    { .lhs = PARSE_NT_##_lhs, .rhs = { no, no, no, no, no, no, no, t1, } },
#define r2(_lhs, t1, t2) \
    { .lhs = PARSE_NT_##_lhs, .rhs = { no, no, no, no, no, no, t1, t2, } },
#define r3(_lhs, t1, t2, t3) \
    { .lhs = PARSE_NT_##_lhs, .rhs = { no, no, no, no, no, t1, t2, t3, } },
#define r4(_lhs, t1, t2, t3, t4) \
    { .lhs = PARSE_NT_##_lhs, .rhs = { no, no, no, no, t1, t2, t3, t4, } },
#define r5(_lhs, t1, t2, t3, t4, t5) \
    { .lhs = PARSE_NT_##_lhs, .rhs = { no, no, no, t1, t2, t3, t4, t5, } },
#define r6(_lhs, t1, t2, t3, t4, t5, t6) \
    { .lhs = PARSE_NT_##_lhs, .rhs = { no, no, t1, t2, t3, t4, t5, t6, } },
#define r7(_lhs, t1, t2, t3, t4, t5, t6, t7) \
    { .lhs = PARSE_NT_##_lhs, .rhs = { no, t1, t2, t3, t4, t5, t6, t7, } },

static const struct rule grammar[] = {
    r3(Unit, t(FBEG), m(Stmt), t(FEND)                                         )

    r5(Stmt, t(WAIT), n(Expr), t(WFOR), n(Expr), t(SCOL)                       )
    r6(Stmt, t(WAIT), n(Expr), t(WFOR), n(Expr), t(WNOB), t(SCOL)              )
    r5(Stmt, t(WAIT), n(Expr), t(WUNT), n(Expr), t(SCOL)                       )
    r6(Stmt, t(WAIT), n(Expr), t(WUNT), n(Expr), t(WNOB), t(SCOL)              )
    r3(Stmt, t(WAIT), n(Expr), t(SCOL)                                         )
    r4(Stmt, t(WAIT), n(Expr), t(WNOB), t(SCOL)                                )
    r3(Stmt, t(RETN), n(Expr), t(SCOL)                                         )
    r2(Stmt, t(RETN), t(SCOL)                                                  )
    r4(Stmt, t(EXPO), t(TYPE), n(Expr), t(SCOL)                                )
    r3(Stmt, t(TYPE), n(Expr), t(SCOL)                                         )

    r2(Ctrl, n(Cond), m(Elif)                                                  )
    r3(Ctrl, n(Cond), m(Elif), n(Else)                                         )
    r1(Ctrl, n(Dowh)                                                           )
    r1(Ctrl, n(Whil)                                                           )

    r5(Cond, t(COND), n(Expr), t(LBRC), m(Stmt), t(RBRC)                       )
    r5(Elif, t(ELIF), n(Expr), t(LBRC), m(Stmt), t(RBRC)                       )
    r4(Else, t(ELSE), t(LBRC), m(Stmt), t(RBRC)                                )

    r7(Dowh, t(DOWH), t(LBRC), m(Stmt), t(RBRC), t(WHIL), n(Expr), t(SCOL)     )
    r5(Whil, t(WHIL), n(Expr), t(LBRC), m(Stmt), t(RBRC)                       )

    r3(Stmt, m(Qual), n(Expr), t(SCOL)                                         )
    r1(Stmt, n(Ctrl)                                                           )
    r1(Stmt, n(Func)                                                           )

    r5(Func, m(Qual), n(Expr), t(LBRC), m(Stmt), t(RBRC)                       )

    r4(Stmt, t(NOIN), t(LBRC), m(Stmt), t(RBRC)                                )
    r3(Stmt, t(LBRC), m(Stmt), t(RBRC)                                         )

    r1(Qual, t(CONS)                                                           )
    r1(Qual, t(EXPO)                                                           )
    r1(Qual, t(STAT)                                                           )

    r1(Atom, t(NAME)                                                           )
    r1(Atom, t(NMBR)                                                           )
    r1(Atom, t(STRL)                                                           )

    r1(Expr, n(Atom)                                                           )
    r1(Expr, n(Fexp)                                                           )
    r1(Expr, n(Pexp)                                                           )
    r1(Expr, n(Texp)                                                           )
    r1(Expr, n(Bexp)                                                           )
    r1(Expr, n(Uexp)                                                           )
    r1(Expr, n(Xexp)                                                           )
    r1(Expr, n(Wexp)                                                           )
    r1(Expr, n(Aexp)                                                           )

    r4(Fexp, n(Expr), t(LPAR), n(Expr), t(RPAR)                                )
    r3(Fexp, n(Expr), t(LPAR), t(RPAR)                                         )

    r3(Pexp, t(LPAR), n(Expr), t(RPAR)                                         )

    r5(Texp, n(Expr), t(QUES), n(Expr), t(COLN), n(Expr)                       )

    r3(Bexp, n(Expr), t(ASSN), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASPL), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASMI), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASMU), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASDI), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASMO), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASLS), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASRS), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASAN), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASXO), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ASOR), n(Expr)                                         )
    r3(Bexp, n(Expr), t(COLN), n(Expr)                                         )
    r3(Bexp, n(Expr), t(SCOP), n(Expr)                                         )
    r3(Bexp, n(Expr), t(ATSI), n(Expr)                                         )
    r3(Bexp, n(Expr), t(MEMB), n(Expr)                                         )
    r3(Bexp, n(Expr), t(AROW), n(Expr)                                         )
    r3(Bexp, n(Expr), t(EQUL), n(Expr)                                         )
    r3(Bexp, n(Expr), t(NEQL), n(Expr)                                         )
    r3(Bexp, n(Expr), t(LTHN), n(Expr)                                         )
    r3(Bexp, n(Expr), t(GTHN), n(Expr)                                         )
    r3(Bexp, n(Expr), t(LTEQ), n(Expr)                                         )
    r3(Bexp, n(Expr), t(GTEQ), n(Expr)                                         )
    r3(Bexp, n(Expr), t(CONJ), n(Expr)                                         )
    r3(Bexp, n(Expr), t(DISJ), n(Expr)                                         )
    r3(Bexp, n(Expr), t(PLUS), n(Expr)                                         )
    r3(Bexp, n(Expr), t(MINS), n(Expr)                                         )
    r3(Bexp, n(Expr), t(MULT), n(Expr)                                         )
    r3(Bexp, n(Expr), t(DIVI), n(Expr)                                         )
    r3(Bexp, n(Expr), t(MODU), n(Expr)                                         )
    r3(Bexp, n(Expr), t(LSHF), n(Expr)                                         )
    r3(Bexp, n(Expr), t(RSHF), n(Expr)                                         )
    r3(Bexp, n(Expr), t(AMPS), n(Expr)                                         )
    r3(Bexp, n(Expr), t(CARE), n(Expr)                                         )
    r3(Bexp, n(Expr), t(PIPE), n(Expr)                                         )
    r3(Bexp, n(Expr), t(COMA), n(Expr)                                         )
    r3(Bexp, n(Expr), t(CAST), n(Expr)                                         )

    r2(Uexp, t(PLUS), n(Expr)                                                  )
    r2(Uexp, t(MINS), n(Expr)                                                  )
    r2(Uexp, t(EXCL), n(Expr)                                                  )
    r2(Uexp, t(TILD), n(Expr)                                                  )
    r2(Uexp, t(MULT), n(Expr)                                                  )
    r2(Uexp, t(AMPS), n(Expr)                                                  )
    r2(Uexp, t(CARE), n(Expr)                                                  )
    r2(Uexp, t(INCR), n(Expr)                                                  )
    r2(Uexp, t(DECR), n(Expr)                                                  )
    r2(Uexp, t(SZOF), n(Expr)                                                  )
    r2(Uexp, t(ALOF), n(Expr)                                                  )

    r2(Xexp, n(Expr), t(INCR)                                                  )
    r2(Xexp, n(Expr), t(DECR)                                                  )

    r2(Wexp, n(Expr), t(WMSE)                                                  )
    r2(Wexp, n(Expr), t(WSEC)                                                  )

    r4(Aexp, n(Expr), t(LBRA), n(Expr), t(RBRA)                                )

    r3(Stmt, t(LBRA), n(Expr), t(RBRA)                                         )
};

#undef r1
#undef r2
#undef r3
#undef r4
#undef r5
#undef r6
#undef r7

#undef n
#undef m
#undef t
#undef no

static const uint8_t precedence[] = {
    /* =   */ 14,
    /* +=  */ 14,
    /* -=  */ 14,
    /* *=  */ 14,
    /* /=  */ 14,
    /* %=  */ 14,
    /* <<= */ 14,
    /* >>= */ 14,
    /* &=  */ 14,
    /* ^=  */ 14,
    /* |=  */ 14,
    /* :   */  1,
    /* ::  */  0,
    /* @   */  2,
    /* .   */  1,
    /* ->  */  1,
    /* ==  */  7,
    /* !=  */  7,
    /* <   */  6,
    /* >   */  6,
    /* <=  */  6,
    /* >=  */  6,
    /* &&  */ 11,
    /* ||  */ 12,
    /* +   */  4,
    /* -   */  4,
    /* *   */  3,
    /* /   */  3,
    /* %   */  3,
    /* <<  */  5,
    /* >>  */  5,
    /* &   */  8,
    /* ^   */  9,
    /* |   */ 10,
    /* ,   */ 15,
    /* as  */  2,
    /* ?:  */ 13,
};

static_assert(sizeof(precedence) == LEX_TK_QUES - LEX_TK_ASSN + 1, "mismatch");

static void destroy_node(const struct parse_node *const node)
{
    if (likely(node->nchildren)) {
        for (size_t child_idx = 0; child_idx < node->nchildren; ++child_idx) {
            destroy_node(node->children[child_idx]);
        }

        free(node->children[0]);
        free(node->children);
    }
}

static void deallocate_stack(void)
{
    free(stack.nodes);
    stack.nodes = NULL;
    stack.size = 0;
    stack.allocated = 0;
}

static void destroy_stack(void)
{
    for (size_t node_idx = 0; node_idx < stack.size; ++node_idx) {
        destroy_node(&stack.nodes[node_idx]);
    }

    deallocate_stack();
}

static inline bool term_eq_node(const struct term *const term,
    const struct parse_node *const node)
{
    const bool node_is_leaf = node->nchildren == 0;

    if (term->is_tk == node_is_leaf) {
        if (node_is_leaf) {
            return term->tk == node->token->tk;
        } else {
            return term->nt == node->nt;
        }
    }

    return false;
}

static size_t match_rule(const struct rule *const rule, size_t *const at)
{
    const struct term *prev = NULL;
    const struct term *term = &rule->rhs[RULE_RHS_LAST];
    ssize_t st_idx = (ssize_t) stack.size - 1;

    do {
        if (term_eq_node(term, &stack.nodes[st_idx])) {
            prev = term->is_mt ? term : NULL;
            --term, --st_idx;
        } else if (prev && term_eq_node(prev, &stack.nodes[st_idx])) {
            --st_idx;
        } else if (term->is_mt) {
            prev = NULL;
            --term;
        } else {
            prev = NULL;
            term = NULL;
            break;
        }
    } while (st_idx >= 0 && !(term->is_tk && term->tk == LEX_TK_COUNT));

    const bool reached_eor = term && term->is_tk && term->tk == LEX_TK_COUNT;

    if (reached_eor) {
        if (prev) {
            while (st_idx >= 0 && term_eq_node(prev, &stack.nodes[st_idx])) {
                --st_idx;
            }
        }

        const size_t reduction_size = stack.size - (size_t) st_idx - 1;
        return *at = (size_t) st_idx + 1, reduction_size;
    } else {
        return 0;
    }
}

static inline int shift(const struct lex_token *const token)
{
    if (unlikely(stack.size >= stack.allocated)) {
        stack.allocated = (stack.allocated ?: 1) * 8;

        struct parse_node *const tmp = realloc(stack.nodes,
            stack.allocated * sizeof(struct parse_node));

        if (unlikely(!tmp)) {
            return PARSE_NOMEM;
        }

        stack.nodes = tmp;
    }

    stack.nodes[stack.size++] = (struct parse_node) {
        .nchildren = 0,
        .token = token,
    };

    return PARSE_OK;
}

static inline bool should_shift_pre(const struct rule *const rule,
    const struct lex_token *const tokens, size_t *const token_idx)
{
    if (unlikely(rule->lhs == PARSE_NT_Unit)) {
        return false;
    }

    while (skip_token(tokens[*token_idx].tk)) {
        ++*token_idx;
    }

    const lex_tk_t ahead = tokens[*token_idx].tk;

    if (rule->lhs == PARSE_NT_Bexp) {
        const lex_tk_t op = rule->rhs[RULE_RHS_LAST - 1].tk;

        if (ahead >= LEX_TK_ASSN && ahead <= LEX_TK_QUES) {
            const uint8_t p1 = precedence[op - LEX_TK_ASSN];
            const uint8_t p2 = precedence[ahead - LEX_TK_ASSN];

            if (p1 > p2) {
                return true;
            }

            if (p1 == p2) {
                return lex_tk_is_assn(op) ||
                    op == LEX_TK_COLN || op == LEX_TK_COMA || op == LEX_TK_SCOP;
            }
        } else if (ahead == LEX_TK_LPAR || ahead == LEX_TK_LBRA ||
            ahead == LEX_TK_INCR || ahead == LEX_TK_DECR) {

            switch (op) {
            case LEX_TK_SCOP:
            case LEX_TK_MEMB:
            case LEX_TK_AROW:
                return false;

            default:
                return true;
            }
        }
    } else if (rule->lhs == PARSE_NT_Uexp) {
        switch (ahead) {
        case LEX_TK_LPAR:
        case LEX_TK_LBRA:
        case LEX_TK_SCOP:
        case LEX_TK_COLN:
        case LEX_TK_MEMB:
        case LEX_TK_AROW:
        case LEX_TK_ATSI:
        case LEX_TK_INCR:
        case LEX_TK_DECR:
            return true;
        }
    } else if (rule->lhs == PARSE_NT_Texp) {
        if (lex_tk_is_assn(ahead) || ahead == LEX_TK_COMA ||
            ahead == LEX_TK_RPAR || ahead == LEX_TK_RBRA || ahead == LEX_TK_SCOL) {

            return false;
        } else {
            return true;
        }
    } else if (rule->lhs == PARSE_NT_Stmt && rule->rhs[RULE_RHS_LAST].is_tk &&
        rule->rhs[RULE_RHS_LAST].tk == LEX_TK_RBRC) {

        /* fixme: { } while 0 { } causes a parse error: check whether "do" exists */
        if (ahead == LEX_TK_WHIL) {
            return true;
        }
    } else if (rule->lhs == PARSE_NT_Qual) {
        if (rule->rhs[RULE_RHS_LAST].tk == LEX_TK_EXPO && ahead == LEX_TK_TYPE) {
            return true;
        }
    }

    return false;
}

static inline bool should_shift_post(const struct rule *const rule,
    const struct lex_token *const tokens, size_t *const token_idx)
{
    if (unlikely(rule->lhs == PARSE_NT_Unit)) {
        return false;
    }

    while (skip_token(tokens[*token_idx].tk)) {
        ++*token_idx;
    }

    if (rule->lhs == PARSE_NT_Cond || rule->lhs == PARSE_NT_Elif) {
        const lex_tk_t ahead = tokens[*token_idx].tk;

        if (ahead == LEX_TK_ELIF || ahead == LEX_TK_ELSE) {
            return true;
        }
    }

    return false;
}

static int reduce(const struct rule *const rule,
    const size_t at, const size_t size)
{
    struct parse_node *const child_nodes =
        malloc(size * sizeof(struct parse_node));

    if (unlikely(!child_nodes)) {
        return PARSE_NOMEM;
    }

    struct parse_node *const reduce_at = &stack.nodes[at];
    struct parse_node **const old_children = reduce_at->children;
    reduce_at->children = malloc(size * sizeof(struct node *)) ?: old_children;

    if (unlikely(reduce_at->children == old_children)) {
        return free(child_nodes), PARSE_NOMEM;
    }

    for (size_t child_idx = 0, st_idx = at;
        st_idx < stack.size;
        ++st_idx, ++child_idx) {

        child_nodes[child_idx] = stack.nodes[st_idx];
        reduce_at->children[child_idx] = &child_nodes[child_idx];
    }

    child_nodes[0].children = old_children;
    reduce_at->nchildren = size;
    reduce_at->nt = rule->lhs;
    stack.size = at + 1;
    return PARSE_OK;
}

static void print_localised_parse_error(FILE *const out,
    const struct parse_node *node)
{
    for (; node->nchildren; node = node->children[0]);

    size_t line, col;
    lex_locate_linecol(node->token, &line, &col);

    fprintf(out, "%s:%zu:%zu: parse error, unexpected ",
        lex_current_file, line, col);

    const struct lex_token *const tok = node->token;

    if (unlikely(tok->tk == LEX_TK_FEND)) {
        fprintf(out, "end of file\n");
    } else {
        fprintf(out, RED("%.*s") "\n", (int) (tok->end - tok->beg), tok->beg);
    }
}

static void diagnose_error(void)
{
    struct status {
        const struct term *prev, *beg, *pos, *end;
        size_t prefix_len;
        bool accepted;
    };

    struct status statuses[GRAMMAR_SIZE];

    for (size_t rule_idx = 0; rule_idx < GRAMMAR_SIZE; ++rule_idx) {
        const struct term *beg = grammar[rule_idx].rhs;

        while (++beg && beg->is_tk && beg->tk == LEX_TK_COUNT);

        statuses[rule_idx] = (struct status) {
            .prev = NULL,
            .beg = beg,
            .pos = beg,
            .end = grammar[rule_idx].rhs + RULE_RHS_LAST + 1,
            .prefix_len = 0,
            .accepted = true,
        };
    }

    size_t st_idx = 0;

    do {
        const struct parse_node *const node = &stack.nodes[st_idx];
        bool did_accept = false;

        for (size_t rule_idx = 0; rule_idx < GRAMMAR_SIZE; ++rule_idx) {
            struct status *const status = &statuses[rule_idx];

            if (status->accepted) {
                match_again:
                if (term_eq_node(status->pos, node)) {
                    did_accept = true;
                    status->prev = status->pos->is_mt ? status->pos : NULL;
                    status->pos++;
                    status->prefix_len++;
                } else if (status->prev && term_eq_node(status->prev, node)) {
                    did_accept = true;
                    status->prefix_len++;
                } else if (status->pos->is_mt) {
                    status->prev = NULL;
                    status->pos++;

                    if (status->pos != status->end) {
                        goto match_again;
                    }
                } else {
                    status->accepted = false;
                }
            }
        }

        if (!did_accept) {
            bool all_unmatched = true;

            for (size_t rule_idx = 0; rule_idx < GRAMMAR_SIZE; ++rule_idx) {
                if (statuses[rule_idx].prefix_len) {
                    all_unmatched = false;
                    break;
                }
            }

            if (all_unmatched) {
                print_localised_parse_error(stderr, node);
                break;
            } else {
                --st_idx;

                for (size_t rule_idx = 0; rule_idx < GRAMMAR_SIZE; ++rule_idx) {
                    statuses[rule_idx].prev = NULL;
                    statuses[rule_idx].pos = statuses[rule_idx].beg;
                    statuses[rule_idx].prefix_len = 0;
                    statuses[rule_idx].accepted = true;
                }
            }
        }
    } while (++st_idx < stack.size);
}

struct parse_node parse(const struct lex_token *const tokens, const size_t ntokens)
{
    static const struct lex_token
        reject = { .tk = PARSE_REJECT },
        nomem  = { .tk = PARSE_NOMEM  };

    static const struct parse_node
        err_reject = { .nchildren = 0, .token = &reject },
        err_nomem  = { .nchildren = 0, .token = &nomem  };

    #define SHIFT_OR_NOMEM(t) \
        if (unlikely(shift(t))) { \
            fprintf(stderr, "out of memory while parsing (shift)\n"); \
            return destroy_stack(), err_nomem; \
        }

    #define REDUCE_OR_NOMEM(r, a, s) \
        if (unlikely(reduce(r, a, s))) { \
            fprintf(stderr, "out of memory while parsing (reduce)\n"); \
            return destroy_stack(), err_nomem; \
        }

    for (size_t token_idx = 0; token_idx < ntokens; ) {
        if (skip_token(tokens[token_idx].tk)) {
            ++token_idx;
            continue;
        }

        SHIFT_OR_NOMEM(&tokens[token_idx++]);

        try_reduce_again:;
        const struct rule *rule = grammar;

        do {
            size_t reduction_at, reduction_size;

            if ((reduction_size = match_rule(rule, &reduction_at))) {
                const bool do_shift = should_shift_pre(rule, tokens, &token_idx);

                if (!do_shift) {
                    REDUCE_OR_NOMEM(rule, reduction_at, reduction_size);
                }

                if (do_shift || should_shift_post(rule, tokens, &token_idx)) {
                    SHIFT_OR_NOMEM(&tokens[token_idx++]);
                }

                goto try_reduce_again;
            }
        } while (++rule != grammar + GRAMMAR_SIZE);
    }

    #undef SHIFT_OR_NOMEM
    #undef REDUCE_OR_NOMEM

    const bool accepted = stack.size == 1 &&
        stack.nodes[0].nchildren && stack.nodes[0].nt == PARSE_NT_Unit;

    if (accepted) {
        const struct parse_node ret = stack.nodes[0];
        return deallocate_stack(), ret;
    } else {
        return diagnose_error(), destroy_stack(), err_reject;
    }
}

void parse_tree_destroy(const struct parse_node root)
{
    destroy_node(&root);
}

void parse_node_ltok_rtok(const struct parse_node *const node,
    const struct lex_token **const ltok, const struct lex_token **const rtok)
{
    const struct parse_node *lhs = node, *rhs = node;

    for (; lhs->nchildren; lhs = lhs->children[0]);
    for (; rhs->nchildren; rhs = rhs->children[rhs->nchildren - 1]);

    *ltok = lhs->token, *rtok = rhs->token;
}
