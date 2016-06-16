#include "lex.h"

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

enum {
    STS_ACCEPT,
    STS_REJECT,
    STS_HUNGRY,
};

typedef uint8_t sts_t;

#define TR(st, tr) (*s = (st), (STS_##tr))
#define REJECT TR(0, REJECT)

#define IS_ALPHA(c)  (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define IS_DIGIT(c)  ((c) >= '0' && (c) <= '9')
#define IS_ALNUM(c)  (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_WSPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')

#define TOKEN_DEFINE_1(token, str) \
static sts_t token(const uint8_t c, uint8_t *const s) \
{ \
    switch (*s) { \
    case 0: return c == (str)[0] ? TR(1, ACCEPT) : REJECT; \
    case 1: return REJECT; \
    default: assert(0), abort(); \
    } \
}

#define TOKEN_DEFINE_2(token, str) \
static sts_t token(const uint8_t c, uint8_t *const s) \
{ \
    switch (*s) { \
    case 0: return c == (str)[0] ? TR(1, HUNGRY) : REJECT; \
    case 1: return c == (str)[1] ? TR(2, ACCEPT) : REJECT; \
    case 2: return REJECT; \
    default: assert(0), abort(); \
    } \
}

#define TOKEN_DEFINE_3(token, str) \
static sts_t token(const uint8_t c, uint8_t *const s) \
{ \
    switch (*s) { \
    case 0: return c == (str)[0] ? TR(1, HUNGRY) : REJECT; \
    case 1: return c == (str)[1] ? TR(2, HUNGRY) : REJECT; \
    case 2: return c == (str)[2] ? TR(3, ACCEPT) : REJECT; \
    case 3: return REJECT; \
    default: assert(0), abort(); \
    } \
}

#define TOKEN_DEFINE_4(token, str) \
static sts_t token(const uint8_t c, uint8_t *const s) \
{ \
    switch (*s) { \
    case 0: return c == (str)[0] ? TR(1, HUNGRY) : REJECT; \
    case 1: return c == (str)[1] ? TR(2, HUNGRY) : REJECT; \
    case 2: return c == (str)[2] ? TR(3, HUNGRY) : REJECT; \
    case 3: return c == (str)[3] ? TR(4, ACCEPT) : REJECT; \
    case 4: return REJECT; \
    default: assert(0), abort(); \
    } \
}

#define TOKEN_DEFINE_5(token, str) \
static sts_t token(const uint8_t c, uint8_t *const s) \
{ \
    switch (*s) { \
    case 0: return c == (str)[0] ? TR(1, HUNGRY) : REJECT; \
    case 1: return c == (str)[1] ? TR(2, HUNGRY) : REJECT; \
    case 2: return c == (str)[2] ? TR(3, HUNGRY) : REJECT; \
    case 3: return c == (str)[3] ? TR(4, HUNGRY) : REJECT; \
    case 4: return c == (str)[4] ? TR(5, ACCEPT) : REJECT; \
    case 5: return REJECT; \
    default: assert(0), abort(); \
    } \
}

#define TOKEN_DEFINE_6(token, str) \
static sts_t token(const uint8_t c, uint8_t *const s) \
{ \
    switch (*s) { \
    case 0: return c == (str)[0] ? TR(1, HUNGRY) : REJECT; \
    case 1: return c == (str)[1] ? TR(2, HUNGRY) : REJECT; \
    case 2: return c == (str)[2] ? TR(3, HUNGRY) : REJECT; \
    case 3: return c == (str)[3] ? TR(4, HUNGRY) : REJECT; \
    case 4: return c == (str)[4] ? TR(5, HUNGRY) : REJECT; \
    case 5: return c == (str)[5] ? TR(6, ACCEPT) : REJECT; \
    case 6: return REJECT; \
    default: assert(0), abort(); \
    } \
}

#define TOKEN_DEFINE_7(token, str) \
static sts_t token(const uint8_t c, uint8_t *const s) \
{ \
    switch (*s) { \
    case 0: return c == (str)[0] ? TR(1, HUNGRY) : REJECT; \
    case 1: return c == (str)[1] ? TR(2, HUNGRY) : REJECT; \
    case 2: return c == (str)[2] ? TR(3, HUNGRY) : REJECT; \
    case 3: return c == (str)[3] ? TR(4, HUNGRY) : REJECT; \
    case 4: return c == (str)[4] ? TR(5, HUNGRY) : REJECT; \
    case 5: return c == (str)[5] ? TR(6, HUNGRY) : REJECT; \
    case 6: return c == (str)[6] ? TR(7, ACCEPT) : REJECT; \
    case 7: return REJECT; \
    default: assert(0), abort(); \
    } \
}

static sts_t tk_name(const uint8_t c, uint8_t *const s)
{
    enum {
        tk_name_begin,
        tk_name_accum,
    };

    switch (*s) {
    case tk_name_begin:
        return IS_ALPHA(c) || (c == '_') ? TR(tk_name_accum, ACCEPT) : REJECT;

    case tk_name_accum:
        return IS_ALNUM(c) || (c == '_') ? STS_ACCEPT : REJECT;
    }

    assert(0), abort();
}

static sts_t tk_nmbr(const uint8_t c, __attribute__((unused)) uint8_t *const s)
{
    return IS_DIGIT(c) ? STS_ACCEPT : STS_REJECT;
}

static sts_t tk_strl(const uint8_t c, uint8_t *const s)
{
    enum {
        tk_strl_begin,
        tk_strl_accum,
        tk_strl_end,
    };

    switch (*s) {
    case tk_strl_begin:
        return c == '"' ? TR(tk_strl_accum, HUNGRY) : REJECT;

    case tk_strl_accum:
        return c != '"' ? STS_HUNGRY : TR(tk_strl_end, ACCEPT);

    case tk_strl_end:
        return REJECT;
    }

    assert(0), abort();
}

static sts_t tk_wspc(const uint8_t c, uint8_t *const s)
{
    enum {
        tk_wspc_begin,
        tk_wspc_accum,
    };

    switch (*s) {
    case tk_wspc_begin:
        return IS_WSPACE(c) ? TR(tk_wspc_accum, ACCEPT) : REJECT;

    case tk_wspc_accum:
        return IS_WSPACE(c) ? STS_ACCEPT : REJECT;
    }

    assert(0), abort();
}

static sts_t tk_lcom(const uint8_t c, uint8_t *const s)
{
    enum {
        tk_lcom_begin,
        tk_lcom_first_slash,
        tk_lcom_accum,
        tk_lcom_end
    };

    switch (*s) {
    case tk_lcom_begin:
        return c == '/' ? TR(tk_lcom_first_slash, HUNGRY) : REJECT;

    case tk_lcom_first_slash:
        return c == '/' ? TR(tk_lcom_accum, HUNGRY) : REJECT;

    case tk_lcom_accum:
        return c == '\n' || c == '\r' ? TR(tk_lcom_end, ACCEPT) : STS_HUNGRY;

    case tk_lcom_end:
        return REJECT;
    }

    assert(0), abort();
}

static sts_t tk_bcom(const uint8_t c, uint8_t *const s)
{
    enum {
        tk_bcom_begin,
        tk_bcom_open_slash,
        tk_bcom_accum,
        tk_bcom_close_star,
        tk_bcom_end
    };

    switch (*s) {
    case tk_bcom_begin:
        return c == '/' ? TR(tk_bcom_open_slash, HUNGRY) : REJECT;

    case tk_bcom_open_slash:
        return c == '*' ? TR(tk_bcom_accum, HUNGRY) : REJECT;

    case tk_bcom_accum:
        return c != '*' ? STS_HUNGRY : TR(tk_bcom_close_star, HUNGRY);

    case tk_bcom_close_star:
        return c == '/' ? TR(tk_bcom_end, ACCEPT) : TR(tk_bcom_accum, HUNGRY);

    case tk_bcom_end:
        return REJECT;
    }

    assert(0), abort();
}

TOKEN_DEFINE_1(tk_lpar, "(")
TOKEN_DEFINE_1(tk_rpar, ")")
TOKEN_DEFINE_1(tk_lbra, "[")
TOKEN_DEFINE_1(tk_rbra, "]")
TOKEN_DEFINE_1(tk_lbrc, "{")
TOKEN_DEFINE_1(tk_rbrc, "}")

TOKEN_DEFINE_2(tk_cond, "if")
TOKEN_DEFINE_4(tk_elif, "elif")
TOKEN_DEFINE_4(tk_else, "else")
TOKEN_DEFINE_2(tk_dowh, "do")
TOKEN_DEFINE_5(tk_whil, "while")
TOKEN_DEFINE_6(tk_retn, "return")
TOKEN_DEFINE_3(tk_useu, "use")
TOKEN_DEFINE_4(tk_type, "type")

TOKEN_DEFINE_1(tk_assn, "=")
TOKEN_DEFINE_2(tk_aspl, "+=")
TOKEN_DEFINE_2(tk_asmi, "-=")
TOKEN_DEFINE_2(tk_asmu, "*=")
TOKEN_DEFINE_2(tk_asdi, "/=")
TOKEN_DEFINE_2(tk_asmo, "%=")
TOKEN_DEFINE_3(tk_asls, "<<=")
TOKEN_DEFINE_3(tk_asrs, ">>=")
TOKEN_DEFINE_2(tk_asan, "&=")
TOKEN_DEFINE_2(tk_asxo, "^=")
TOKEN_DEFINE_2(tk_asor, "|=")
TOKEN_DEFINE_1(tk_coln, ":")
TOKEN_DEFINE_2(tk_scop, "::")
TOKEN_DEFINE_1(tk_atsi, "@")
TOKEN_DEFINE_1(tk_memb, ".")
TOKEN_DEFINE_2(tk_arow, "->")
TOKEN_DEFINE_2(tk_equl, "==")
TOKEN_DEFINE_2(tk_neql, "!=")
TOKEN_DEFINE_1(tk_lthn, "<")
TOKEN_DEFINE_1(tk_gthn, ">")
TOKEN_DEFINE_2(tk_lteq, "<=")
TOKEN_DEFINE_2(tk_gteq, ">=")
TOKEN_DEFINE_2(tk_conj, "&&")
TOKEN_DEFINE_2(tk_disj, "||")
TOKEN_DEFINE_1(tk_plus, "+")
TOKEN_DEFINE_1(tk_mins, "-")
TOKEN_DEFINE_1(tk_mult, "*")
TOKEN_DEFINE_1(tk_divi, "/")
TOKEN_DEFINE_1(tk_modu, "%")
TOKEN_DEFINE_2(tk_lshf, "<<")
TOKEN_DEFINE_2(tk_rshf, ">>")
TOKEN_DEFINE_1(tk_amps, "&")
TOKEN_DEFINE_1(tk_care, "^")
TOKEN_DEFINE_1(tk_pipe, "|")
TOKEN_DEFINE_1(tk_coma, ",")
TOKEN_DEFINE_2(tk_cast, "as")
TOKEN_DEFINE_1(tk_ques, "?")

TOKEN_DEFINE_1(tk_excl, "!")
TOKEN_DEFINE_1(tk_tild, "~")
TOKEN_DEFINE_2(tk_incr, "++")
TOKEN_DEFINE_2(tk_decr, "--")
TOKEN_DEFINE_6(tk_szof, "sizeof")
TOKEN_DEFINE_7(tk_alof, "alignof")

TOKEN_DEFINE_4(tk_wait, "wait")
TOKEN_DEFINE_3(tk_wfor, "for")
TOKEN_DEFINE_5(tk_wunt, "until")
TOKEN_DEFINE_7(tk_wnob, "noblock")
TOKEN_DEFINE_4(tk_wmse, "msec")
TOKEN_DEFINE_3(tk_wsec, "sec")
TOKEN_DEFINE_5(tk_noin, "noint")

TOKEN_DEFINE_1(tk_scol, ";")

TOKEN_DEFINE_5(tk_cons, "const")
TOKEN_DEFINE_7(tk_expo, "exposed")
TOKEN_DEFINE_6(tk_stat, "static")

static sts_t (*const tk_funcs[])(const uint8_t, uint8_t *const) = {
    tk_name,
    tk_nmbr,
    tk_strl,

    tk_wspc,
    tk_lcom,
    tk_bcom,

    tk_lpar,
    tk_rpar,
    tk_lbra,
    tk_rbra,
    tk_lbrc,
    tk_rbrc,

    tk_cond,
    tk_elif,
    tk_else,
    tk_dowh,
    tk_whil,
    tk_retn,
    tk_useu,
    tk_type,

    tk_assn,
    tk_aspl,
    tk_asmi,
    tk_asmu,
    tk_asdi,
    tk_asmo,
    tk_asls,
    tk_asrs,
    tk_asan,
    tk_asxo,
    tk_asor,
    tk_coln,
    tk_scop,
    tk_atsi,
    tk_memb,
    tk_arow,
    tk_equl,
    tk_neql,
    tk_lthn,
    tk_gthn,
    tk_lteq,
    tk_gteq,
    tk_conj,
    tk_disj,
    tk_plus,
    tk_mins,
    tk_mult,
    tk_divi,
    tk_modu,
    tk_lshf,
    tk_rshf,
    tk_amps,
    tk_care,
    tk_pipe,
    tk_coma,
    tk_cast,
    tk_ques,

    tk_excl,
    tk_tild,
    tk_incr,
    tk_decr,
    tk_szof,
    tk_alof,

    tk_wait,
    tk_wfor,
    tk_wunt,
    tk_wnob,
    tk_wmse,
    tk_wsec,
    tk_noin,

    tk_scol,

    tk_cons,
    tk_expo,
    tk_stat,
};

static_assert(countof(tk_funcs) == LEX_TK_COUNT, "mismatch");

static inline int push_token(struct lex_token **const tokens,
    size_t *const ntokens, size_t *const allocated, const lex_tk_t token,
    const uint8_t *const beg, const uint8_t *const end)
{
    if (unlikely(*ntokens >= *allocated)) {
        *allocated = (*allocated ?: 1) * 8;

        struct lex_token *const tmp =
            realloc(*tokens, *allocated * sizeof(struct lex_token));

        if (unlikely(!tmp)) {
            return free(*tokens), *tokens = NULL, LEX_NOMEM;
        }

        *tokens = tmp;
    }

    (*tokens)[(*ntokens)++] = (struct lex_token) {
        .beg = beg,
        .end = end,
        .tk = token
    };

    return LEX_OK;
}

const char *lex_current_file;

int lex(const uint8_t *const input, const size_t size,
    struct lex_token **const tokens, size_t *const ntokens)
{
    static struct {
        sts_t prev, curr;
    } statuses[LEX_TK_COUNT] = {
        [0 ... LEX_TK_COUNT - 1] = { STS_HUNGRY, STS_REJECT }
    };

    uint8_t states[LEX_TK_COUNT] = {0};

    const uint8_t *prefix_beg = input, *prefix_end = input;
    lex_tk_t accepted_token;
    size_t allocated = 0;
    *tokens = NULL, *ntokens = 0;

    #define PUSH_OR_NOMEM(tk, beg, end) \
        if (unlikely(push_token(tokens, ntokens, &allocated, (tk), (beg), (end)))) { \
            fprintf(stderr, "out of memory while lexing\n"); \
            return LEX_NOMEM; \
        }

    #define print_last_token lex_print_error(stderr, "unknown token", \
        &(*tokens)[*ntokens - 1], &(*tokens)[*ntokens - 1])

    #define foreach_tk \
        for (lex_tk_t tk = 0; tk < LEX_TK_COUNT; ++tk)

    PUSH_OR_NOMEM(LEX_TK_FBEG, NULL, NULL);

    while (prefix_end < input + size) {
        int did_accept = 0;

        foreach_tk {
            if (statuses[tk].prev != STS_REJECT) {
                statuses[tk].curr = tk_funcs[tk](*prefix_end, &states[tk]);
            }

            if (statuses[tk].curr != STS_REJECT) {
                did_accept = 1;
            }
        }

        if (did_accept) {
            prefix_end++;

            foreach_tk {
                statuses[tk].prev = statuses[tk].curr;
            }
        } else {
            accepted_token = LEX_TK_COUNT;

            foreach_tk {
                if (statuses[tk].prev == STS_ACCEPT) {
                    accepted_token = tk;
                }

                statuses[tk].prev = STS_HUNGRY;
                statuses[tk].curr = STS_REJECT;
            }

            PUSH_OR_NOMEM(accepted_token, prefix_beg, prefix_end);

            if (unlikely(accepted_token == LEX_TK_COUNT)) {
                (*tokens)[*ntokens - 1].end++;
                return print_last_token, LEX_UNKNOWN_TOKEN;
            }

            prefix_beg = prefix_end;
        }
    }

    accepted_token = LEX_TK_COUNT;

    foreach_tk {
        if (statuses[tk].curr == STS_ACCEPT) {
            accepted_token = tk;
        }

        statuses[tk].prev = STS_HUNGRY;
        statuses[tk].curr = STS_REJECT;
    }

    PUSH_OR_NOMEM(accepted_token, prefix_beg, prefix_end);

    if (unlikely(accepted_token == LEX_TK_COUNT)) {
        return print_last_token, LEX_UNKNOWN_TOKEN;
    }

    PUSH_OR_NOMEM(LEX_TK_FEND, NULL, NULL);
    return LEX_OK;

    #undef PUSH_OR_NOMEM
    #undef print_last_token
    #undef foreach_tk
}

bool lex_symbols_equal(const struct lex_symbol *const sym1,
    const struct lex_symbol *const sym2)
{
    const size_t len1 = (size_t) (sym1->end - sym1->beg);
    const size_t len2 = (size_t) (sym2->end - sym2->beg);

    return len1 == len2 && !memcmp(sym1->beg, sym2->beg, len1);
}

void lex_locate_linecol(const struct lex_token *token,
    size_t *const line, size_t *const col)
{
    *line = 1, *col = 1;

    for (--token; token->tk != LEX_TK_FBEG; --token) {
        const uint8_t *character = token->end - 1;

        do {
            if (*character == '\n' || *character == '\r') {
                ++*line;
            } else if (*line == 1) {
                ++*col;
            }
        } while (--character != token->beg - 1);
    }
}

void lex_print_symbol(FILE *const out, const char *const fmt,
    const struct lex_symbol *const sym)
{
    const ptrdiff_t len = sym->end - sym->beg;
    fprintf(out, fmt ?: "%.*s", (int) len, sym->beg);
}

void lex_print_error(FILE *const out, const char *const desc,
    const struct lex_token *ltok, const struct lex_token *const rtok)
{
    size_t line, col;
    lex_locate_linecol(ltok, &line, &col);
    fprintf(out, "%s:%zu:%zu: %s: ", lex_current_file, line, col, desc);

    for (; ltok <= rtok; ++ltok) {
        fprintf(out, RED("%.*s"), (int) (ltok->end - ltok->beg), ltok->beg);
    }

    fputs("\n", out);
}
