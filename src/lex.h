#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

enum {
    LEX_TK_NAME, // name
    LEX_TK_NMBR, // decimal integer literal
    LEX_TK_STRL, // string literal

    LEX_TK_WSPC, // whitespace
    LEX_TK_LCOM, // line comment
    LEX_TK_BCOM, // block comment

    LEX_TK_LPAR, // left parenthesis
    LEX_TK_RPAR, // right parenthesis
    LEX_TK_LBRA, // left bracket
    LEX_TK_RBRA, // right bracket
    LEX_TK_LBRC, // left brace
    LEX_TK_RBRC, // right brace

    LEX_TK_COND, // if keyword
    LEX_TK_ELIF, // elif keyword
    LEX_TK_ELSE, // else keyword
    LEX_TK_DOWH, // do keyword
    LEX_TK_WHIL, // while keyword
    LEX_TK_RETN, // return keyword
    LEX_TK_USEU, // use keyword
    LEX_TK_TYPE, // type keyword

    LEX_TK_ASSN, // (binary) assignment
    LEX_TK_ASPL, // (binary) assignment-plus
    LEX_TK_ASMI, // (binary) assignment-minus
    LEX_TK_ASMU, // (binary) assignment-multiply
    LEX_TK_ASDI, // (binary) assignment-divide
    LEX_TK_ASMO, // (binary) assignment-modulo
    LEX_TK_ASLS, // (binary) assignment-left shift
    LEX_TK_ASRS, // (binary) assignment-right shift
    LEX_TK_ASAN, // (binary) assignment-and
    LEX_TK_ASXO, // (binary) assignment-xor
    LEX_TK_ASOR, // (binary) assignment-or
    LEX_TK_COLN, // (binary) colon separator
    LEX_TK_SCOP, // (binary) scope
    LEX_TK_ATSI, // (binary) quaint is at label test
    LEX_TK_MEMB, // (binary) struct/union member
    LEX_TK_AROW, // (binary) deref struct/union member
    LEX_TK_EQUL, // (binary) equality test
    LEX_TK_NEQL, // (binary) inequality test
    LEX_TK_LTHN, // (binary) less than
    LEX_TK_GTHN, // (binary) greater than
    LEX_TK_LTEQ, // (binary) less than or equal
    LEX_TK_GTEQ, // (binary) greater than or equal
    LEX_TK_CONJ, // (binary) logical and
    LEX_TK_DISJ, // (binary) logical or
    LEX_TK_PLUS, // (binary, unary) addition, identity
    LEX_TK_MINS, // (binary, unary) subtraction, negation
    LEX_TK_MULT, // (binary, unary) multiplication, dereference/dequaintify
    LEX_TK_DIVI, // (binary) division
    LEX_TK_MODU, // (binary) modulo
    LEX_TK_LSHF, // (binary) left shift
    LEX_TK_RSHF, // (binary) right shift
    LEX_TK_AMPS, // (binary, unary) bitwise and, address-of
    LEX_TK_CARE, // (binary, unary) bitwise xor, bitwise negation
    LEX_TK_PIPE, // (binary) bitwise or
    LEX_TK_COMA, // (binary) comma operator/separator
    LEX_TK_CAST, // (binary) typecast
    LEX_TK_QUES, // (ternary) ?: conditional operator

    LEX_TK_EXCL, // (unary) logical not
    LEX_TK_TILD, // (unary) quaintify
    LEX_TK_INCR, // (unary) prefix/postfix increment
    LEX_TK_DECR, // (unary) prefix/postfix decrement
    LEX_TK_SZOF, // (unary) size of type
    LEX_TK_ALOF, // (unary) alignment of type

    LEX_TK_WAIT, // wait keyword
    LEX_TK_WFOR, // for keyword
    LEX_TK_WUNT, // until keyword
    LEX_TK_WNOB, // noblock keyword
    LEX_TK_WMSE, // msec keyword
    LEX_TK_WSEC, // sec keyword
    LEX_TK_NOIN, // noint keyword

    LEX_TK_SCOL, // semicolon statement separator

    LEX_TK_CONS, // const (variables only)
    LEX_TK_EXPO, // exposed (variables and functions)
    LEX_TK_STAT, // static (variables in function scope only)

    LEX_TK_COUNT,
    LEX_TK_FBEG, // beginning of file marker
    LEX_TK_FEND, // end of file marker
};

static_assert(LEX_TK_COUNT + 2 <= UINT8_MAX, "");
typedef uint8_t lex_tk_t;

#define lex_tk_is_assn(t) ({ \
    const lex_tk_t t_once = (t); \
    t_once >= LEX_TK_ASSN && t_once <= LEX_TK_ASOR; \
})

struct lex_symbol {
    const uint8_t *beg, *end;
};

#define lex_sym(s) &(const struct lex_symbol) { \
    .beg = (const uint8_t *) (s), \
    .end = (const uint8_t *) (s) + sizeof(s) - 1, \
}

struct lex_token {
    const uint8_t *beg, *end;
    lex_tk_t tk;
};

extern const char *lex_current_file;
int lex(const uint8_t *, size_t, struct lex_token **, size_t *);

enum {
    LEX_OK = 0,
    LEX_NOMEM,
    LEX_UNKNOWN_TOKEN,
};

bool lex_symbols_equal(const struct lex_symbol *, const struct lex_symbol *);
void lex_locate_linecol(const struct lex_token *, size_t *, size_t *);
void lex_print_symbol(FILE *, const char *, const struct lex_symbol *);
void lex_print_error(FILE *, const char *, const struct lex_token *,
    const struct lex_token *);
