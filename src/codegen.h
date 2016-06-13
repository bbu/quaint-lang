#pragma once

struct ast_node;
struct type;

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

enum {
    CODEGEN_OP_NOP = 0,

    CODEGEN_OP_MOV,
    CODEGEN_OP_CAST,

    CODEGEN_OP_ADD,
    CODEGEN_OP_SUB,
    CODEGEN_OP_MUL,
    CODEGEN_OP_DIV,
    CODEGEN_OP_MOD,

    CODEGEN_OP_EQU,
    CODEGEN_OP_NEQ,
    CODEGEN_OP_LT,
    CODEGEN_OP_GT,
    CODEGEN_OP_LTE,
    CODEGEN_OP_GTE,

    CODEGEN_OP_LSH,
    CODEGEN_OP_RSH,
    CODEGEN_OP_AND,
    CODEGEN_OP_XOR,
    CODEGEN_OP_OR,

    CODEGEN_OP_NOT,
    CODEGEN_OP_NEG,
    CODEGEN_OP_BNEG,
    CODEGEN_OP_OZ,
    CODEGEN_OP_INC,
    CODEGEN_OP_DEC,
    CODEGEN_OP_INCP,
    CODEGEN_OP_DECP,

    CODEGEN_OP_JZ,
    CODEGEN_OP_JNZ,
    CODEGEN_OP_JMP,

    CODEGEN_OP_PUSHR,
    CODEGEN_OP_PUSH,
    CODEGEN_OP_CALL,
    CODEGEN_OP_CALLV,
    CODEGEN_OP_INCSP,
    CODEGEN_OP_RET,
    CODEGEN_OP_RETV,

    CODEGEN_OP_REF,
    CODEGEN_OP_DRF,
    CODEGEN_OP_RTE,
    CODEGEN_OP_RTEV,
    CODEGEN_OP_QAT,
    CODEGEN_OP_WAIT,
    CODEGEN_OP_WLAB,
    CODEGEN_OP_GETSP,
    CODEGEN_OP_QNT,
    CODEGEN_OP_QNTV,

    CODEGEN_OP_NOINT,
    CODEGEN_OP_INT,
    CODEGEN_OP_BFUN,

    CODEGEN_OP_COUNT,
};

static_assert(CODEGEN_OP_COUNT - 1 <= UINT8_MAX, "");
typedef uint8_t codegen_op_t;

enum {
    CODEGEN_OPD_IMM,
    CODEGEN_OPD_TEMP,
    CODEGEN_OPD_AUTO,
    CODEGEN_OPD_GLOB,
    CODEGEN_OPD_COUNT,
};

static_assert(CODEGEN_OPD_COUNT - 1 < 4, "");
typedef uint8_t codegen_opd_t;

struct codegen_opd {
    codegen_opd_t opd: 2;
    codegen_opd_t signd: 1;
    codegen_opd_t indirect: 1;

    union {
        /* opd != CODEGEN_OPD_IMM */
        struct {
            uint64_t off, size;
        };

        /* opd == CODEGEN_OPD_IMM */
        struct {
            uint64_t imm, immsize;
        };
    };
} __attribute__((packed));

struct codegen_insn {
    codegen_op_t op;

    union {
        struct {
            struct codegen_opd dst, src1, src2;
        } bin;

        struct {
            struct codegen_opd dst, src;
        } un;

        struct codegen_opd dst;

        struct {
            struct codegen_opd dst, loc, sp;
        } qnt;

        struct {
            struct codegen_opd dst, val;
        } qntv;

        struct {
            struct codegen_opd dst, quaint;
            uintptr_t func;
            uint64_t wlab_id;
        } qat;

        struct {
            struct codegen_opd quaint, timeout;
            uintptr_t func;
            uint64_t wlab_id;
            uint8_t noblock: 1, units: 1, has_timeout: 1;
        } wait;

        struct {
            uintptr_t func;
            uint64_t id;
        } wlab;

        struct {
            struct codegen_opd cond;
            uint64_t loc;
        } jmp;

        struct {
            struct codegen_opd val, ssp;
        } push;

        struct {
            struct codegen_opd val, loc, bp;
        } call;

        struct {
            struct codegen_opd addend, tsize;
        } incsp;

        struct {
            struct codegen_opd val, size;
        } ret;
    };
};

struct codegen_datum {
    const char *name;
    const struct type *type;
    size_t off, size;
};

struct codegen_subr {
    const char *name;
    size_t param_count;
    const struct type **param_types;
    const struct type *rettype;
    size_t off, size;
};

struct codegen_type {
    const char *name;
    const struct type *type;
};

struct codegen_obj {
    struct codegen_subr *exposed_subrs;
    struct codegen_datum *exposed_data;
    struct codegen_type *exposed_types;

    size_t data_size;
    size_t insn_count;

    struct {
        uint8_t *mem;
        size_t size;
    } strings;

    struct codegen_insn *insns;
};

int codegen_obj_create(const struct ast_node *, struct codegen_obj *);
void codegen_obj_destroy(const struct codegen_obj *);

enum {
    CODEGEN_OK = 0,
    CODEGEN_NOMEM,
    CODEGEN_UNRESOLVED,
};
