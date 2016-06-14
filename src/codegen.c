#include "codegen.h"

#include "lex.h"
#include "ast.h"
#include "scope.h"
#include "type.h"
#include "htab.h"

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>

#define NOMEM \
    (fprintf(stderr, "%s:%d: no memory\n", __FILE__, __LINE__), CODEGEN_NOMEM)

#define GEN_BLOK(blok) \
    if (unlikely(gen_blok((blok)))) { \
        return CODEGEN_NOMEM; \
    }

#define GEN_STMT(stmt) \
    if (unlikely(gen_stmt((stmt)))) { \
        return CODEGEN_NOMEM; \
    }

#define GEN_EXPR(expr, res, need_lvalue) \
    if (unlikely(gen_expr((expr), (res), (need_lvalue)))) { \
        return CODEGEN_NOMEM; \
    }

#define OPD_IMM(name, _signd, _imm, _immsize) \
    struct codegen_opd name = { \
        .opd = CODEGEN_OPD_IMM, \
        .signd = (_signd), \
        .indirect = 0, \
        .imm = (_imm), \
        .immsize = (_immsize), \
    }

#define OPD_TEMP(name, _signd, _size) \
    struct codegen_opd name = { \
        .opd = CODEGEN_OPD_TEMP, \
        .signd = (_signd), \
        .indirect = 0, \
        .off = ({ \
            ALIGN_UP(temp_off, (_size) <= 8 ? (_size) : 8); \
            const size_t begin_temp_off = temp_off; \
            temp_off += (_size); \
            \
            if (temp_off > temp_off_peak) { \
                temp_off_peak = temp_off; \
            } \
            \
            begin_temp_off; \
        }), \
        .size = (_size), \
    }

#define OPD_AUTO(name, _signd, _off, _size) \
    struct codegen_opd name = { \
        .opd = CODEGEN_OPD_AUTO, \
        .signd = (_signd), \
        .indirect = 0, \
        .off = (_off), \
        .size = (_size), \
    }

#define OPD_GLOB(name, _signd, _off, _size) \
    struct codegen_opd name = { \
        .opd = CODEGEN_OPD_GLOB, \
        .signd = (_signd), \
        .indirect = 0, \
        .off = (_off), \
        .size = (_size), \
    }

#define SET_DIRECT(opd) { \
    (opd).indirect = 0; \
    (opd).signd = 0; \
    (opd).size = 8; \
}

#define SET_INDIRECT(opd, _signd, _size) { \
    (opd).indirect = 1; \
    (opd).signd = (_signd); \
    (opd).size = (_size); \
}

#define RESERVE_INSN \
    if (unlikely(insn_at(ip))) { \
        return NOMEM; \
    }

#define INSN_BIN(_op, _dst, _src1, _src2) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_##_op, \
        .bin = { .dst = (_dst), .src1 = (_src1), .src2 = (_src2) } \
    }

#define INSN_BIN_V(_op, _dst, _src1, _src2) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = (_op), \
        .bin = { .dst = (_dst), .src1 = (_src1), .src2 = (_src2) } \
    }

#define INSN_UN(_op, _dst, _src) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_##_op, \
        .un = { .dst = (_dst), .src = (_src) } \
    }

#define INSN_UN_V(_op, _dst, _src) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = (_op), \
        .un = { .dst = (_dst), .src = (_src) } \
    }

#define INSN_DST(_op, _dst) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_##_op, \
        .dst = (_dst) \
    }

#define INSN_QNT(_dst, _loc, _sp) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_QNT, \
        .qnt = { .dst = (_dst), .loc = (_loc), .sp = (_sp) } \
    }

#define INSN_QNTV(_dst, _val) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_QNTV, \
        .qntv = { .dst = (_dst), .val = (_val) } \
    }

#define INSN_QAT(_dst, _quaint, _func, _wlab_id) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_QAT, \
        .qat = { (_dst), (_quaint), (_func), (_wlab_id) } \
    }

#define INSN_WAIT(_quaint, _timeout, _func, _wlab_id, _noblock, _units, _has_timeout) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_WAIT, \
        .wait = { \
            (_quaint), (_timeout), (_func), (_wlab_id), \
            (_noblock), (_units), (_has_timeout) \
        } \
    }

#define INSN_WLAB(_func, _id) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_WLAB, \
        .wlab = { .func = (_func), .id = (_id) } \
    }

#define INSN_CJMP(_op, _cond, _loc) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_##_op, \
        .jmp = { .cond = (_cond), .loc = (_loc) } \
    }

#define INSN_JMP(_loc) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_JMP, \
        .jmp = { .loc = (_loc) } \
    }

#define INSN_PUSHR(_val, _ssp) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_PUSHR, \
        .push = { .val = (_val), .ssp = (_ssp) } \
    }

#define INSN_PUSH(_val) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_PUSH, \
        .push = { .val = (_val) } \
    }

#define INSN_CALL(_loc, _bp) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_CALL, \
        .call = { .loc = (_loc), .bp = (_bp) } \
    }

#define INSN_CALLV(_val, _loc, _bp) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_CALLV, \
        .call = { .val = (_val), .loc = (_loc), .bp = (_bp) } \
    }

#define INSN_INCSP(_addend, _tsize) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_INCSP, \
        .incsp = { .addend = (_addend), .tsize = (_tsize) } \
    }

#define INSN_RET(_size) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_RET, \
        .ret = { .size = (_size) } \
    }

#define INSN_RETV(_val, _size) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_RETV, \
        .ret = { .val = (_val), .size = (_size) } \
    }

#define INSN(_op) \
    RESERVE_INSN o->insns[ip++] = (struct codegen_insn) { \
        .op = CODEGEN_OP_##_op \
    }

static size_t insn_size, strings_mem_size, ip, temp_off, temp_off_peak;

struct ofs {
    size_t off, size;
};

struct func_tag {
    size_t frame_size, args_size;
    uint64_t loc;
    struct htab *layout;
};

static struct htab *globals, *funcs;
static struct func_tag *ftag;
static struct codegen_obj *o;

static const uint64_t const_values[] = {
    [SCOPE_BCON_ID_NULL] = 0,
    [SCOPE_BCON_ID_TRUE] = 1,
    [SCOPE_BCON_ID_FALSE] = 0,
};

static_assert(countof(const_values) == SCOPE_BCON_ID_COUNT, "const mismatch");

static void funcs_dtor(const void *const ptr)
{
    const struct func_tag *const tag = ptr;
    htab_destroy(tag->layout, htab_default_dtor);
    free((void *) tag);
}

static inline void count_top_decls_and_funcs(const struct ast_node *const root,
    size_t *const decl_count, size_t *const func_count)
{
    const struct ast_unit *const unit = ast_data(root, unit);
    *decl_count = 0, *func_count = 0;

    for (size_t idx = 0; idx < unit->stmt_count; ++idx) {
        const struct ast_node *const stmt = unit->stmts[idx];

        assert(stmt != NULL);

        switch (stmt->an) {
        case AST_AN_TYPE: break;

        case AST_AN_FUNC:
            (*func_count)++;
            break;

        case AST_AN_DECL: {
            const struct ast_decl *const decl = ast_data(stmt, decl);
            *decl_count += decl->name_count;
        } break;

        default: assert(0), abort();
        }
    }
}

static size_t count_block_decls(const struct ast_node *const node)
{
    assert(node != NULL);

    struct ast_node *const *stmts;
    size_t stmt_count;
    size_t count = 0;

    switch (node->an) {
    case AST_AN_FUNC: {
        const struct ast_func *const func = ast_data(node, func);
        stmts = func->stmts, stmt_count = func->stmt_count;
        count += func->param_count;
    } break;

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

    default: assert(0), abort();
    }

    for (size_t idx = 0; idx < stmt_count; ++idx) {
        assert(stmts[idx] != NULL);
        assert(stmts[idx]->an != AST_AN_VOID);

        switch (stmts[idx]->an) {
        case AST_AN_DECL:
            count += ast_data(stmts[idx], decl)->name_count;
            break;

        case AST_AN_BLOK:
        case AST_AN_NOIN:
        case AST_AN_WHIL:
        case AST_AN_DOWH:
            count += count_block_decls(stmts[idx]);
            break;

        case AST_AN_COND: {
            const struct ast_cond *const cond = ast_data(stmts[idx], cond);
            count += count_block_decls(cond->if_block);

            for (size_t elif_idx = 0; elif_idx < cond->elif_count; ++elif_idx) {
                count += count_block_decls(cond->elif[elif_idx].block);
            }

            if (cond->else_block) {
                count += count_block_decls(cond->else_block);
            }
        } break;
        }
    }

    return count;
}

static int create_frame_layout(const struct ast_node *const node,
    struct func_tag *const tag)
{
    assert(node != NULL);

    struct ast_node *const *stmts;
    size_t stmt_count;

    switch (node->an) {
    case AST_AN_FUNC: {
        const struct ast_func *const func = ast_data(node, func);
        stmts = func->stmts, stmt_count = func->stmt_count;
        tag->frame_size = 0;

        for (size_t idx = 0; idx < func->param_count; ++idx) {
            struct ofs *const ofs = malloc(sizeof(struct ofs));

            if (unlikely(!ofs)) {
                return CODEGEN_NOMEM;
            }

            const struct type *const type = func->params[idx].type;
            ofs->off = tag->frame_size;
            ofs->size = type->count * type->size;
            tag->frame_size += ofs->size;
            ALIGN_UP(tag->frame_size, 8);
            htab_insert(tag->layout, (uintptr_t) func->params[idx].name, ofs);
        }

        tag->args_size = tag->frame_size;
    } break;

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

    default: assert(0), abort();
    }

    for (size_t idx = 0; idx < stmt_count; ++idx) {
        assert(stmts[idx] != NULL);
        assert(stmts[idx]->an != AST_AN_VOID);

        switch (stmts[idx]->an) {
        case AST_AN_DECL: {
            const struct ast_decl *const decl = ast_data(stmts[idx], decl);

            for (size_t name_idx = 0; name_idx < decl->name_count; ++name_idx) {
                struct ofs *const ofs = malloc(sizeof(struct ofs));

                if (unlikely(!ofs)) {
                    return CODEGEN_NOMEM;
                }

                ALIGN_UP(tag->frame_size, decl->type->alignment);
                ofs->off = tag->frame_size;
                ofs->size = decl->type->count * decl->type->size;
                tag->frame_size += ofs->size;
                htab_insert(tag->layout, (uintptr_t) decl->names[name_idx], ofs);
            }
        } break;

        case AST_AN_BLOK:
        case AST_AN_NOIN:
        case AST_AN_WHIL:
        case AST_AN_DOWH:
            if (unlikely(create_frame_layout(stmts[idx], tag))) {
                return CODEGEN_NOMEM;
            }
            break;

        case AST_AN_COND: {
            const struct ast_cond *const cond = ast_data(stmts[idx], cond);

            if (unlikely(create_frame_layout(cond->if_block, tag))) {
                return CODEGEN_NOMEM;
            }

            for (size_t elif_idx = 0; elif_idx < cond->elif_count; ++elif_idx) {
                if (unlikely(create_frame_layout(cond->elif[elif_idx].block, tag))) {
                    return CODEGEN_NOMEM;
                }
            }

            if (cond->else_block) {
                if (unlikely(create_frame_layout(cond->else_block, tag))) {
                    return CODEGEN_NOMEM;
                }
            }
        } break;
        }
    }

    if (node->an == AST_AN_FUNC) {
        ALIGN_UP(tag->frame_size, 8);
    }

    return CODEGEN_OK;
}

static int create_global_and_frame_layouts(const struct ast_node *const root,
    const size_t decl_count, const size_t func_count, size_t *const data_offset)
{
    assert(globals == NULL);
    assert(funcs == NULL);

    if (unlikely(htab_create(&globals, decl_count) ||
        htab_create(&funcs, func_count))) {

        goto out_nomem;
    }

    const struct ast_unit *const unit = ast_data(root, unit);
    *data_offset = 0;

    for (size_t idx = 0; idx < unit->stmt_count; ++idx) {
        const struct ast_node *const stmt = unit->stmts[idx];

        assert(stmt != NULL);

        switch (stmt->an) {
        case AST_AN_TYPE: break;

        case AST_AN_DECL: {
            const struct ast_decl *const decl = ast_data(stmt, decl);

            for (size_t name_idx = 0; name_idx < decl->name_count; ++name_idx) {
                struct ofs *const ofs = malloc(sizeof(struct ofs));

                if (unlikely(!ofs)) {
                    goto out_nomem;
                }

                ALIGN_UP(*data_offset, decl->type->alignment);
                ofs->off = *data_offset;
                ofs->size = decl->type->count * decl->type->size;
                *data_offset += ofs->size;
                htab_insert(globals, (uintptr_t) decl->names[name_idx], ofs);
            }
        } break;

        case AST_AN_FUNC: {
            struct func_tag *const tag = malloc(sizeof(struct func_tag));

            if (unlikely(!tag)) {
                goto out_nomem;
            }

            if (unlikely(htab_create(&tag->layout, count_block_decls(stmt)))) {
                free(tag);
                goto out_nomem;
            }

            htab_insert(funcs, (uintptr_t) stmt, tag);

            if (unlikely(create_frame_layout(stmt, tag))) {
                goto out_nomem;
            }
        } break;

        default: assert(0), abort();
        }
    }

    return CODEGEN_OK;

out_nomem:
    htab_destroy(globals, htab_default_dtor), globals = NULL;
    htab_destroy(funcs, funcs_dtor), funcs = NULL;
    return CODEGEN_NOMEM;
}

static int insn_at(const size_t at)
{
    if (at < insn_size) {
        return CODEGEN_OK;
    }

    const size_t new_insn_size = at ? at * 2 : 8;

    struct codegen_insn *const tmp = realloc(o->insns,
        new_insn_size * sizeof(struct codegen_insn));

    if (unlikely(!tmp)) {
        return CODEGEN_NOMEM;
    }

    o->insns = tmp;
    insn_size = new_insn_size;
    return CODEGEN_OK;
}

static int push_string(const uint8_t *const beg, const uint8_t *const end)
{
    assert(end - beg >= 0);
    const size_t len = (size_t) (end - beg);
    const size_t least_required_size = o->strings.size + len + 1;

    if (least_required_size > strings_mem_size) {
        const size_t new_strings_mem_size = least_required_size * 2;
        uint8_t *const tmp = realloc(o->strings.mem, new_strings_mem_size);

        if (unlikely(!tmp)) {
            return CODEGEN_NOMEM;
        }

        o->strings.mem = tmp;
        strings_mem_size = new_strings_mem_size;
    }

    memcpy(o->strings.mem + o->strings.size, beg, len);
    *(o->strings.mem + o->strings.size + len) = 0;
    o->strings.size += len + 1;
    return CODEGEN_OK;
}

static int gen_blok(const struct ast_node *);
static int gen_stmt(const struct ast_node *);
static int gen_expr(const struct ast_node *, struct codegen_opd *, bool);

static int gen_bexp_assn(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_ASSN);
    struct codegen_opd dst, src;
    GEN_EXPR(bexp->lhs, &dst, true);
    GEN_EXPR(bexp->rhs, &src, false);
    INSN_UN(MOV, dst, src);
    return *result = dst, CODEGEN_OK;
}

static int gen_bexp_asmu(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    codegen_op_t op;

    switch (bexp->op) {
    case LEX_TK_ASMU: op = CODEGEN_OP_MUL; break;
    case LEX_TK_ASDI: op = CODEGEN_OP_DIV; break;
    case LEX_TK_ASMO: op = CODEGEN_OP_MOD; break;
    case LEX_TK_ASLS: op = CODEGEN_OP_LSH; break;
    case LEX_TK_ASRS: op = CODEGEN_OP_RSH; break;
    case LEX_TK_ASAN: op = CODEGEN_OP_AND; break;
    case LEX_TK_ASXO: op = CODEGEN_OP_XOR; break;
    case LEX_TK_ASOR: op = CODEGEN_OP_OR;  break;
    default: assert(0), abort();
    }

    struct codegen_opd res1, res2;
    GEN_EXPR(bexp->lhs, &res1, true);
    GEN_EXPR(bexp->rhs, &res2, false);
    INSN_BIN_V(op, res1, res1, res2);
    return *result = res1, CODEGEN_OK;
}

static int gen_bexp_scop(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_SCOP);
    const size_t size = bexp->type->count * bexp->type->size;

    switch (bexp->type->t) {
    case TYPE_ENUM: {
        const struct ast_node *const value_name = bexp->rhs;
        uint64_t value = 0;

        for (size_t idx = 0; idx < bexp->type->value_count; ++idx) {
            if (lex_symbols_equal(bexp->type->values[idx].name,
                (const struct lex_symbol *) value_name->ltok)) {

                value = bexp->type->values[idx].value;
                break;
            }
        }

        OPD_IMM(src, 0, value, size);
        *result = src;
    } break;

    default: assert(0), abort();
    }

    return CODEGEN_OK;
}

static int gen_bexp_atsi(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_ATSI);
    struct codegen_opd res;
    const size_t size = bexp->type->count * bexp->type->size;
    const uint8_t signd =
        type_is_integral(bexp->type->t) && type_is_signed(bexp->type->t);

    GEN_EXPR(bexp->lhs, &res, false);

    const uintptr_t func = (uintptr_t) bexp->func;
    const uint64_t wlab_id = func ?
        (func != 1 ? bexp->func->wlabs[bexp->wlab_idx].id : 0) : 0;

    OPD_TEMP(dst, signd, size);
    INSN_QAT(dst, res, func, wlab_id);
    return *result = dst, CODEGEN_OK;
}

static int gen_bexp_memb(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_MEMB);
    struct codegen_opd res;

    GEN_EXPR(bexp->lhs, &res, need_lvalue);

    const struct type *const lhs_type = type_of_expr(bexp->lhs);
    const struct type *const memb_type = lhs_type->members[bexp->member_idx].type;
    const size_t offset = lhs_type->offsets[bexp->member_idx];

    const uint8_t memb_signd =
        type_is_integral(memb_type->t) && type_is_signed(memb_type->t);

    const size_t memb_size = memb_type->size * memb_type->count;

    if (!res.indirect) {
        res.off += offset, res.signd = memb_signd, res.size = memb_size;
        return *result = res, CODEGEN_OK;
    }

    SET_DIRECT(res);
    OPD_TEMP(dst_drf, 0, 8);
    INSN_UN(DRF, dst_drf, res);
    OPD_IMM(off, 0, offset, 8);
    OPD_TEMP(dst, 0, 8);
    INSN_BIN(ADD, dst, dst_drf, off);
    SET_INDIRECT(dst, memb_signd, memb_size);
    return *result = dst, CODEGEN_OK;
}

static int gen_bexp_arow(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_AROW);

    struct codegen_opd res;

    GEN_EXPR(bexp->lhs, &res, false);

    if (res.indirect) {
        SET_DIRECT(res);
        OPD_TEMP(dst, 0, 8);
        INSN_UN(DRF, dst, res);
        res = dst;
    }

    const struct type *const lhs_type = (struct type *) type_of_expr(bexp->lhs);

    if (unlikely(type_quantify(lhs_type->subtype))) {
        return NOMEM;
    }

    const struct type *const memb_type =
        lhs_type->subtype->members[bexp->member_idx].type;

    const size_t offset = lhs_type->subtype->offsets[bexp->member_idx];
    const size_t memb_size = memb_type->count * memb_type->size;

    const uint8_t memb_signd =
        type_is_integral(memb_type->t) && type_is_signed(memb_type->t);

    OPD_TEMP(dst, 0, 8);
    OPD_IMM(off, 0, offset, 8);
    INSN_BIN(ADD, dst, res, off);
    SET_INDIRECT(dst, memb_signd, memb_size);
    return *result = dst, CODEGEN_OK;
}

static int gen_bexp_equl(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    codegen_op_t op;

    switch (bexp->op) {
    case LEX_TK_EQUL: op = CODEGEN_OP_EQU; break;
    case LEX_TK_NEQL: op = CODEGEN_OP_NEQ; break;
    case LEX_TK_LTHN: op = CODEGEN_OP_LT;  break;
    case LEX_TK_GTHN: op = CODEGEN_OP_GT;  break;
    case LEX_TK_LTEQ: op = CODEGEN_OP_LTE; break;
    case LEX_TK_GTEQ: op = CODEGEN_OP_GTE; break;
    case LEX_TK_MULT: op = CODEGEN_OP_MUL; break;
    case LEX_TK_DIVI: op = CODEGEN_OP_DIV; break;
    case LEX_TK_MODU: op = CODEGEN_OP_MOD; break;
    case LEX_TK_LSHF: op = CODEGEN_OP_LSH; break;
    case LEX_TK_RSHF: op = CODEGEN_OP_RSH; break;
    case LEX_TK_AMPS: op = CODEGEN_OP_AND; break;
    case LEX_TK_CARE: op = CODEGEN_OP_XOR; break;
    case LEX_TK_PIPE: op = CODEGEN_OP_OR;  break;
    default: assert(0), abort();
    }

    struct codegen_opd res1, res2;
    const size_t size = bexp->type->size * bexp->type->count;
    const uint8_t signd =
        type_is_integral(bexp->type->t) && type_is_signed(bexp->type->t);

    GEN_EXPR(bexp->lhs, &res1, false);
    GEN_EXPR(bexp->rhs, &res2, false);
    OPD_TEMP(dst, signd, size);
    INSN_BIN_V(op, dst, res1, res2);
    return *result = dst, CODEGEN_OK;
}

static int gen_bexp_conj(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_CONJ);
    struct codegen_opd res1, res2;
    const size_t size = bexp->type->count * bexp->type->size;
    const uint8_t signd =
        type_is_integral(bexp->type->t) && type_is_signed(bexp->type->t);

    GEN_EXPR(bexp->lhs, &res1, false);
    OPD_TEMP(dst, signd, size);
    INSN_UN(OZ, dst, res1);
    const size_t jz_ip = ip;
    INSN_CJMP(JZ, res1, 0);
    GEN_EXPR(bexp->rhs, &res2, false);
    INSN_UN(OZ, dst, res2);
    OPD_IMM(one, dst.signd, 1, dst.size);
    INSN_BIN(AND, dst, one, dst);
    o->insns[jz_ip].jmp.loc = ip;
    return *result = dst, CODEGEN_OK;
}

static int gen_bexp_disj(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_DISJ);
    struct codegen_opd res1, res2;
    const size_t size = bexp->type->count * bexp->type->size;
    const uint8_t signd =
        type_is_integral(bexp->type->t) && type_is_signed(bexp->type->t);

    GEN_EXPR(bexp->lhs, &res1, false);
    OPD_TEMP(dst, signd, size);
    INSN_UN(OZ, dst, res1);
    const size_t jnz_ip = ip;
    INSN_CJMP(JNZ, res1, 0);
    GEN_EXPR(bexp->rhs, &res2, false);
    INSN_UN(OZ, dst, res2);
    OPD_IMM(zero, dst.signd, 0, dst.size);
    INSN_BIN(OR, dst, zero, dst);
    o->insns[jnz_ip].jmp.loc = ip;
    return *result = dst, CODEGEN_OK;
}

static int gen_bexp_plus(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);

    assert(bexp->op == LEX_TK_PLUS || bexp->op == LEX_TK_MINS ||
        bexp->op == LEX_TK_ASPL || bexp->op == LEX_TK_ASMI);

    const bool is_assignment = bexp->op == LEX_TK_ASPL || bexp->op == LEX_TK_ASMI;
    struct codegen_opd res1, res2;

    GEN_EXPR(bexp->lhs, &res1, is_assignment);
    GEN_EXPR(bexp->rhs, &res2, false);

    const struct type *const lhs_type = type_of_expr(bexp->lhs);
    size_t multiplier;

    if (lhs_type->t == TYPE_PTR) {
        if (unlikely(type_quantify(lhs_type->subtype))) {
            return NOMEM;
        }

        multiplier = lhs_type->subtype->count * lhs_type->subtype->size;
    } else {
        multiplier = 1;
    }

    if (multiplier != 1) {
        OPD_TEMP(dst, 0, 8);
        OPD_IMM(mult, 0, multiplier, 8);
        INSN_BIN(MUL, dst, res2, mult);
        res2 = dst;
    }

    const size_t size = bexp->type->count * bexp->type->size;
    OPD_TEMP(dst, res1.signd, size);

    switch (bexp->op) {
    case LEX_TK_PLUS:
        INSN_BIN(ADD, dst, res1, res2);
        break;

    case LEX_TK_MINS:
        INSN_BIN(SUB, dst, res1, res2);
        break;

    case LEX_TK_ASPL:
        INSN_BIN(ADD, res1, res1, res2);
        break;

    case LEX_TK_ASMI:
        INSN_BIN(SUB, res1, res1, res2);
        break;
    }

    return *result = is_assignment ? res1 : dst, CODEGEN_OK;
}

static int gen_bexp_coma(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_COMA);
    struct codegen_opd res_unused;
    const size_t saved_temp_off = temp_off;
    GEN_EXPR(bexp->lhs, &res_unused, false);
    temp_off = saved_temp_off;
    GEN_EXPR(bexp->rhs, result, false);
    return CODEGEN_OK;
}

static int gen_bexp_cast(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_bexp *const bexp = ast_data(expr, bexp);
    assert(bexp->op == LEX_TK_CAST || bexp->op == LEX_TK_COLN);

    if (unlikely(type_quantify(bexp->cast))) {
        return NOMEM;
    }

    const size_t size = bexp->cast->count * bexp->cast->size;
    const uint8_t signd =
        type_is_integral(bexp->cast->t) && type_is_signed(bexp->cast->t);

    struct codegen_opd res;
    GEN_EXPR(bexp->lhs, &res, false);
    OPD_TEMP(dst, signd, size);
    INSN_UN(CAST, dst, res);
    return *result = dst, CODEGEN_OK;
}

static int gen_uexp_plus(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_uexp *const uexp = ast_data(expr, uexp);
    assert(uexp->op == LEX_TK_PLUS);
    GEN_EXPR(uexp->rhs, result, false);
    return CODEGEN_OK;
}

static int gen_uexp_mins(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_uexp *const uexp = ast_data(expr, uexp);
    codegen_op_t op;

    switch (uexp->op) {
    case LEX_TK_MINS: op = CODEGEN_OP_NEG;  break;
    case LEX_TK_EXCL: op = CODEGEN_OP_NOT;  break;
    case LEX_TK_CARE: op = CODEGEN_OP_BNEG; break;
    default: assert(0), abort();
    }

    struct codegen_opd res;
    const size_t size = uexp->type->count * uexp->type->size;
    const uint8_t signd =
        type_is_integral(uexp->type->t) && type_is_signed(uexp->type->t);

    GEN_EXPR(uexp->rhs, &res, false);
    OPD_TEMP(dst, signd, size);
    INSN_UN_V(op, dst, res);
    return *result = dst, CODEGEN_OK;
}

static int gen_uexp_tild(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_uexp *const uexp = ast_data(expr, uexp);
    assert(uexp->op == LEX_TK_TILD);
    const size_t size = uexp->type->count * uexp->type->size;

    OPD_TEMP(dst, 0, size);

    if (uexp->rhs->an == AST_AN_FEXP) {
        const struct ast_fexp *const fexp = ast_data(uexp->rhs, fexp);
        const struct ast_node *arglist = fexp->rhs;
        OPD_TEMP(ssp, 0, 8);
        INSN_DST(GETSP, ssp);

        while (arglist) {
            const uint8_t not_last = arglist->an == AST_AN_BEXP &&
                ast_data(arglist, bexp)->op == LEX_TK_COMA;

            const struct ast_node *const arg = not_last ?
                ast_data(arglist, bexp)->lhs : arglist;

            arglist = not_last ? ast_data(arglist, bexp)->rhs : NULL;

            struct codegen_opd arg_res;
            GEN_EXPR(arg, &arg_res, false);
            INSN_PUSH(arg_res);
        }

        struct codegen_opd lhs_res;
        GEN_EXPR(fexp->lhs, &lhs_res, false);
        INSN_QNT(dst, lhs_res, ssp);
    } else {
        struct codegen_opd res;
        GEN_EXPR(uexp->rhs, &res, false);
        INSN_QNTV(dst, res);
    }

    return *result = dst, CODEGEN_OK;
}

static int gen_uexp_mult(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    const struct ast_uexp *const uexp = ast_data(expr, uexp);
    assert(uexp->op == LEX_TK_MULT);
    struct codegen_opd res;
    GEN_EXPR(uexp->rhs, &res, false);

    const bool is_ptr = type_of_expr(uexp->rhs)->t == TYPE_PTR;
    const size_t size = uexp->type->count * uexp->type->size;
    const uint8_t signd =
        type_is_integral(uexp->type->t) && type_is_signed(uexp->type->t);

    if (is_ptr && need_lvalue) {
        SET_INDIRECT(res, signd, size);
        return *result = res, CODEGEN_OK;
    }

    if (is_ptr) {
        OPD_TEMP(dst, signd, size);
        INSN_UN(DRF, dst, res);
        *result = dst;
    } else if (size) {
        OPD_TEMP(dst, signd, size);
        INSN_UN(RTEV, dst, res);
        *result = dst;
    } else {
        struct codegen_opd dst;
        memset(&dst, 0, sizeof(dst));
        INSN_UN(RTE, dst, res);
    }

    return CODEGEN_OK;
}

static int gen_uexp_amps(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_uexp *const uexp = ast_data(expr, uexp);
    assert(uexp->op == LEX_TK_AMPS);
    struct codegen_opd res;
    const size_t size = uexp->type->count * uexp->type->size;

    GEN_EXPR(uexp->rhs, &res, false);
    OPD_TEMP(dst, 0, size);
    INSN_UN(REF, dst, res);
    return *result = dst, CODEGEN_OK;
}

static int gen_uexp_incr(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_uexp *const uexp = ast_data(expr, uexp);
    assert(uexp->op == LEX_TK_INCR || uexp->op == LEX_TK_DECR);

    struct codegen_opd res;
    GEN_EXPR(uexp->rhs, &res, true);

    const size_t size = uexp->type->size * uexp->type->count;
    const uint8_t signd =
        type_is_integral(uexp->type->t) && type_is_signed(uexp->type->t);

    const struct type *const rhs_type = type_of_expr(uexp->rhs);
    size_t step;

    if (rhs_type->t == TYPE_PTR) {
        if (unlikely(type_quantify(rhs_type->subtype))) {
            return NOMEM;
        }

        step = rhs_type->subtype->count * rhs_type->subtype->size;
    } else {
        step = 1;
    }

    if (step == 1) {
        if (uexp->op == LEX_TK_INCR) {
            INSN_DST(INC, res);
        } else {
            INSN_DST(DEC, res);
        }
    } else {
        OPD_IMM(addend, signd, step, size);

        if (uexp->op == LEX_TK_INCR) {
            INSN_BIN(ADD, res, res, addend);
        } else {
            INSN_BIN(SUB, res, res, addend);
        }
    }

    return *result = res, CODEGEN_OK;
}

static int gen_uexp_szof(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_uexp *const uexp = ast_data(expr, uexp);
    assert(uexp->op == LEX_TK_SZOF || uexp->op == LEX_TK_ALOF);

    if (unlikely(type_quantify(uexp->typespec))) {
        return NOMEM;
    }

    const uint64_t value = uexp->op == LEX_TK_SZOF ?
        uexp->typespec->size * uexp->typespec->count : uexp->typespec->alignment;

    OPD_IMM(dst, 0, value, 8);
    return *result = dst, CODEGEN_OK;
}

static int gen_fexp(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_fexp *const fexp = ast_data(expr, fexp);
    const size_t size = fexp->type->size * fexp->type->count;
    const uint8_t signd =
        type_is_integral(fexp->type->t) && type_is_signed(fexp->type->t);

    OPD_IMM(addr, 0, 0, 8);
    OPD_TEMP(ssp, 0, 8);
    const size_t pushr_ip = ip;
    INSN_PUSHR(addr, ssp);

    const struct ast_node *arglist = fexp->rhs;

    while (arglist) {
        const struct ast_node *arg;

        if (arglist->an == AST_AN_BEXP && ast_data(arglist, bexp)->op == LEX_TK_COMA) {
            arg = ast_data(arglist, bexp)->lhs;
            arglist = ast_data(arglist, bexp)->rhs;
        } else {
            arg = arglist;
            arglist = NULL;
        }

        struct codegen_opd arg_res;
        GEN_EXPR(arg, &arg_res, false);
        INSN_PUSH(arg_res);
    }

    struct codegen_opd lhs_res;
    GEN_EXPR(fexp->lhs, &lhs_res, false);
    o->insns[pushr_ip].push.val.imm = ip;

    if (size) {
        OPD_TEMP(val, signd, size);
        INSN_CALLV(val, lhs_res, ssp);
        *result = val;
    } else {
        INSN_CALL(lhs_res, ssp);
    }

    return CODEGEN_OK;
}

static int gen_xexp_incr(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_xexp *const xexp = ast_data(expr, xexp);
    assert(xexp->op == LEX_TK_INCR || xexp->op == LEX_TK_DECR);
    struct codegen_opd res;

    GEN_EXPR(xexp->lhs, &res, true);

    const size_t size = xexp->type->size * xexp->type->count;
    const uint8_t signd =
        type_is_integral(xexp->type->t) && type_is_signed(xexp->type->t);

    const struct type *const lhs_type = type_of_expr(xexp->lhs);
    size_t step;

    if (lhs_type->t == TYPE_PTR) {
        if (unlikely(type_quantify(lhs_type->subtype))) {
            return NOMEM;
        }

        step = lhs_type->subtype->count * lhs_type->subtype->size;
    } else {
        step = 1;
    }

    OPD_TEMP(dst, signd, size);

    if (step == 1) {
        if (xexp->op == LEX_TK_INCR) {
            INSN_UN(INCP, dst, res);
        } else {
            INSN_UN(DECP, dst, res);
        }
    } else {
        OPD_IMM(addend, signd, step, size);
        INSN_UN(MOV, dst, res);

        if (xexp->op == LEX_TK_INCR) {
            INSN_BIN(ADD, res, res, addend);
        } else {
            INSN_BIN(SUB, res, res, addend);
        }
    }

    return *result = dst, CODEGEN_OK;
}

static int gen_aexp(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    assert(expr->an == AST_AN_AEXP);
    const struct ast_aexp *const aexp = ast_data(expr, aexp);
    struct codegen_opd res_base, res_off;

    GEN_EXPR(aexp->base, &res_base, need_lvalue);
    GEN_EXPR(aexp->off, &res_off, false);

    const struct type *const aexp_type = aexp->type;
    const size_t off_size = type_of_expr(aexp->off)->size;

    const size_t elem_size = aexp_type->count * aexp_type->size;
    const uint8_t elem_signd =
        type_is_integral(aexp_type->t) && type_is_signed(aexp_type->t);

    OPD_TEMP(idx_scaled, 0, 8);

    if (elem_size == 1) {
        if (off_size == 8) {
            idx_scaled = res_off;
        } else {
            INSN_UN(CAST, idx_scaled, res_off);
        }
    } else {
        if (off_size != 8) {
            INSN_UN(CAST, idx_scaled, res_off);
        }

        OPD_IMM(mult, 0, elem_size, 8);
        INSN_BIN(MUL, idx_scaled, off_size != 8 ? idx_scaled : res_off, mult);
    }

    if (res_base.indirect) {
        SET_DIRECT(res_base);
        OPD_TEMP(dst, 0, 8);
        INSN_BIN(ADD, dst, res_base, idx_scaled);
        SET_INDIRECT(dst, elem_signd, elem_size);
        return *result = dst, CODEGEN_OK;
    }

    OPD_TEMP(ref_dst, 0, 8);
    INSN_UN(REF, ref_dst, res_base);
    OPD_TEMP(arr_dst, 0, 8);
    INSN_BIN(ADD, arr_dst, ref_dst, idx_scaled);
    SET_INDIRECT(arr_dst, elem_signd, elem_size);
    return *result = arr_dst, CODEGEN_OK;
}

static int gen_texp(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_texp *const texp = ast_data(expr, texp);
    struct codegen_opd cond_res, tval_res, fval_res;
    const size_t size = texp->type->size * texp->type->count;
    const uint8_t signd =
        type_is_integral(texp->type->t) && type_is_signed(texp->type->t);

    GEN_EXPR(texp->cond, &cond_res, false);
    OPD_TEMP(res, signd, size);
    const size_t jz_ip = ip;
    INSN_CJMP(JZ, cond_res, 0);
    const size_t saved_temp_off = temp_off;
    GEN_EXPR(texp->tval, &tval_res, false);
    INSN_UN(MOV, res, tval_res);
    const size_t jmp_ip = ip;
    INSN_JMP(0);
    o->insns[jz_ip].jmp.loc = ip;
    temp_off = saved_temp_off;
    GEN_EXPR(texp->fval, &fval_res, false);
    INSN_UN(MOV, res, fval_res);
    o->insns[jmp_ip].jmp.loc = ip;
    return *result = res, CODEGEN_OK;
}

static int gen_nmbr(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_nmbr *const nmbr = ast_data(expr, nmbr);
    const type_t t = nmbr->type->t;
    const uint8_t signd = type_is_integral(t) && type_is_signed(t);
    OPD_IMM(src, signd, nmbr->value, nmbr->type->size);
    return *result = src, CODEGEN_OK;
}

static int gen_strl(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_strl *const strl = ast_data(expr, strl);
    const size_t str_beg = o->data_size + o->strings.size;

    if (unlikely(push_string(strl->str.beg, strl->str.end))) {
        return NOMEM;
    }

    OPD_GLOB(src, 0, str_beg, 1);
    OPD_TEMP(dst, 0, 8);
    INSN_UN(REF, dst, src);
    return *result = dst, CODEGEN_OK;
}

static int gen_name(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    (void) need_lvalue;
    const struct ast_name *const name = ast_data(expr, name);
    assert(name->scoped != NULL);
    const struct scope_obj *const scoped = name->scoped;
    const uintptr_t key = (uintptr_t) scoped->name;
    const type_t t = name->type->t;
    const uint8_t signd = type_is_integral(t) && type_is_signed(t);

    switch (scoped->obj) {
    case SCOPE_OBJ_GVAR: {
        const struct ofs *const ofs = htab_get(globals, key);
        OPD_GLOB(src, signd, ofs->off, ofs->size);
        *result = src;
    } break;

    case SCOPE_OBJ_AVAR:
    case SCOPE_OBJ_PARM: {
        const struct ofs *const ofs = htab_get(ftag->layout, key);
        OPD_AUTO(src, signd, ofs->off, ofs->size);
        *result = src;
    } break;

    case SCOPE_OBJ_BCON: {
        assert(scoped->bcon_id < SCOPE_BCON_ID_COUNT);
        const uint64_t value = const_values[scoped->bcon_id];
        OPD_IMM(src, signd, value, name->type->size * name->type->count);
        *result = src;
    } break;

    case SCOPE_OBJ_BFUN: {
        assert(scoped->bfun_id < SCOPE_BFUN_ID_COUNT);
        OPD_IMM(src, 0, scoped->bfun_id, 8);
        *result = src;
    } break;

    case SCOPE_OBJ_FUNC: {
        OPD_IMM(src, 0, (uintptr_t) scoped->func, 0);
        *result = src;
    } break;

    default: assert(0), abort();
    }

    return CODEGEN_OK;
}

static int gen_expr(const struct ast_node *const expr,
    struct codegen_opd *const result, const bool need_lvalue)
{
    assert(expr != NULL);
    assert(ftag->layout != NULL);
    assert(o != NULL);
    assert(result != NULL);

    int (*gen)(const struct ast_node *, struct codegen_opd *, bool) = NULL;

    switch (expr->an) {
    case AST_AN_BEXP: {
        const struct ast_bexp *const bexp = ast_data(expr, bexp);

        switch (bexp->op) {
        case LEX_TK_ASSN: gen = gen_bexp_assn; break;
        case LEX_TK_ASPL:
        case LEX_TK_ASMI: gen = gen_bexp_plus; break;
        case LEX_TK_ASMU:
        case LEX_TK_ASDI:
        case LEX_TK_ASMO:
        case LEX_TK_ASLS:
        case LEX_TK_ASRS:
        case LEX_TK_ASAN:
        case LEX_TK_ASXO:
        case LEX_TK_ASOR: gen = gen_bexp_asmu; break;
        case LEX_TK_COLN: gen = gen_bexp_cast; break;
        case LEX_TK_SCOP: gen = gen_bexp_scop; break;
        case LEX_TK_ATSI: gen = gen_bexp_atsi; break;
        case LEX_TK_MEMB: gen = gen_bexp_memb; break;
        case LEX_TK_AROW: gen = gen_bexp_arow; break;
        case LEX_TK_EQUL:
        case LEX_TK_NEQL:
        case LEX_TK_LTHN:
        case LEX_TK_GTHN:
        case LEX_TK_LTEQ:
        case LEX_TK_GTEQ: gen = gen_bexp_equl; break;
        case LEX_TK_CONJ: gen = gen_bexp_conj; break;
        case LEX_TK_DISJ: gen = gen_bexp_disj; break;
        case LEX_TK_PLUS:
        case LEX_TK_MINS: gen = gen_bexp_plus; break;
        case LEX_TK_MULT:
        case LEX_TK_DIVI:
        case LEX_TK_MODU:
        case LEX_TK_LSHF:
        case LEX_TK_RSHF:
        case LEX_TK_AMPS:
        case LEX_TK_CARE:
        case LEX_TK_PIPE: gen = gen_bexp_equl; break;
        case LEX_TK_COMA: gen = gen_bexp_coma; break;
        case LEX_TK_CAST: gen = gen_bexp_cast; break;
        default: assert(0), abort();
        }
    } break;

    case AST_AN_UEXP: {
        const struct ast_uexp *const uexp = ast_data(expr, uexp);

        switch (uexp->op) {
        case LEX_TK_PLUS: gen = gen_uexp_plus; break;
        case LEX_TK_MINS:
        case LEX_TK_EXCL: gen = gen_uexp_mins; break;
        case LEX_TK_TILD: gen = gen_uexp_tild; break;
        case LEX_TK_MULT: gen = gen_uexp_mult; break;
        case LEX_TK_AMPS: gen = gen_uexp_amps; break;
        case LEX_TK_CARE: gen = gen_uexp_mins; break;
        case LEX_TK_INCR:
        case LEX_TK_DECR: gen = gen_uexp_incr; break;
        case LEX_TK_SZOF:
        case LEX_TK_ALOF: gen = gen_uexp_szof; break;
        default: assert(0), abort();
        }
    } break;

    case AST_AN_FEXP: gen = gen_fexp; break;

    case AST_AN_XEXP: {
        const struct ast_xexp *const xexp = ast_data(expr, xexp);

        switch (xexp->op) {
        case LEX_TK_INCR:
        case LEX_TK_DECR: gen = gen_xexp_incr; break;
        default: assert(0), abort();
        }
    } break;

    case AST_AN_AEXP: gen = gen_aexp; break;
    case AST_AN_TEXP: gen = gen_texp; break;

    case AST_AN_NAME: gen = gen_name; break;
    case AST_AN_NMBR: gen = gen_nmbr; break;
    case AST_AN_STRL: gen = gen_strl; break;

    default: assert(0), abort();
    }

    assert(gen != NULL);
    return gen(expr, result, need_lvalue);
}

static int gen_decl_auto(const struct ast_node *const stmt)
{
    assert(stmt->an == AST_AN_DECL);
    const struct ast_decl *const decl = ast_data(stmt, decl);

    if (!decl->init_expr) {
        return CODEGEN_OK;
    }

    struct codegen_opd init_res;
    GEN_EXPR(decl->init_expr, &init_res, false);
    const type_t t = decl->type->t;
    const uint8_t signd = type_is_integral(t) && type_is_signed(t);

    for (size_t idx = 0; idx < decl->name_count; ++idx) {
        const uintptr_t key = (uintptr_t) decl->names[idx];
        const struct ofs *const ofs = htab_get(ftag->layout, key);
        OPD_AUTO(dst, signd, ofs->off, ofs->size);
        INSN_UN(MOV, dst, init_res);
    }

    return CODEGEN_OK;
}

static int gen_blok(const struct ast_node *const stmt)
{
    assert(stmt->an == AST_AN_BLOK || stmt->an == AST_AN_NOIN);
    const struct ast_blok *const blok = ast_data(stmt, blok);
    const bool is_noint = stmt->an == AST_AN_NOIN;

    if (is_noint) {
        INSN(NOINT);
    }

    for (size_t idx = 0; idx < blok->stmt_count; ++idx) {
        GEN_STMT(blok->stmts[idx]);
    }

    if (is_noint) {
        INSN(INT);
    }

    return CODEGEN_OK;
}

static int gen_whil(const struct ast_node *const stmt)
{
    assert(stmt->an == AST_AN_WHIL);
    const struct ast_whil *const whil = ast_data(stmt, whil);
    struct codegen_opd cond_res;

    const size_t jmp_ip = ip;
    GEN_EXPR(whil->expr, &cond_res, false);
    const size_t jz_ip = ip;
    INSN_CJMP(JZ, cond_res, 0);
    temp_off = 0;

    for (size_t idx = 0; idx < whil->stmt_count; ++idx) {
        GEN_STMT(whil->stmts[idx]);
    }

    INSN_JMP(jmp_ip);
    o->insns[jz_ip].jmp.loc = ip;
    return CODEGEN_OK;
}

static int gen_dowh(const struct ast_node *const stmt)
{
    assert(stmt->an == AST_AN_DOWH);
    const struct ast_dowh *const dowh = ast_data(stmt, dowh);
    const size_t jnz_ip = ip;

    for (size_t idx = 0; idx < dowh->stmt_count; ++idx) {
        GEN_STMT(dowh->stmts[idx]);
    }

    struct codegen_opd cond_res;
    GEN_EXPR(dowh->expr, &cond_res, false);
    INSN_CJMP(JNZ, cond_res, jnz_ip);
    return CODEGEN_OK;
}

static int gen_cond(const struct ast_node *const stmt)
{
    assert(stmt->an == AST_AN_COND);
    const struct ast_cond *const cond = ast_data(stmt, cond);

    struct codegen_opd if_cond_res;
    GEN_EXPR(cond->if_expr, &if_cond_res, false);
    size_t prev_jmp_ip = ip;
    INSN_CJMP(JZ, if_cond_res, 0);
    temp_off = 0;
    GEN_BLOK(cond->if_block);
    size_t end_jmp_ips[1 + cond->elif_count];
    end_jmp_ips[0] = ip;
    INSN_JMP(0);

    for (size_t idx = 0; idx < cond->elif_count; ++idx) {
        struct codegen_opd elif_cond_res;
        o->insns[prev_jmp_ip].jmp.loc = ip;
        GEN_EXPR(cond->elif[idx].expr, &elif_cond_res, false);
        prev_jmp_ip = ip;
        INSN_CJMP(JZ, elif_cond_res, 0);
        temp_off = 0;
        GEN_BLOK(cond->elif[idx].block);
        end_jmp_ips[1 + idx] = ip;
        INSN_JMP(0);
    }

    o->insns[prev_jmp_ip].jmp.loc = ip;

    if (cond->else_block) {
        GEN_BLOK(cond->else_block);
    }

    for (size_t idx = 0; idx < 1 + cond->elif_count; ++idx) {
        o->insns[end_jmp_ips[idx]].jmp.loc = ip;
    }

    return CODEGEN_OK;
}

static int gen_retn(const struct ast_node *const stmt)
{
    assert(stmt->an == AST_AN_RETN);
    const struct ast_retn *const retn = ast_data(stmt, retn);
    OPD_IMM(size, 0, ftag->frame_size + 16, 8);

    if (retn->expr) {
        struct codegen_opd val;
        GEN_EXPR(retn->expr, &val, false);
        INSN_RETV(val, size);
    } else {
        INSN_RET(size);
    }

    return CODEGEN_OK;
}

static int gen_wait(const struct ast_node *const stmt)
{
    assert(stmt->an == AST_AN_WAIT);
    const struct ast_wait *const wait = ast_data(stmt, wait);

    struct codegen_opd res_quaint;
    GEN_EXPR(wait->wquaint, &res_quaint, false);
    OPD_IMM(res_timeout, 0, 0, 1);

    if (wait->wfor) {
        GEN_EXPR(wait->wfor, &res_timeout, false);
    }

    const uintptr_t func = (uintptr_t) wait->func;
    const uint64_t wlab_id = wait->func ?
        wait->func->wlabs[wait->wlab_idx].id : 0;

    INSN_WAIT(res_quaint, res_timeout, func, wlab_id,
        wait->noblock, wait->units, wait->wfor ? 1 : 0);

    return CODEGEN_OK;
}

static int gen_wlab(const struct ast_node *const stmt)
{
    assert(stmt->an == AST_AN_WLAB);
    const struct ast_wlab *const wlab = ast_data(stmt, wlab);
    assert(wlab->func != 0);
    assert(wlab->id != 0);
    INSN_WLAB(wlab->func, wlab->id);
    return CODEGEN_OK;
}

static int gen_stmt(const struct ast_node *const stmt)
{
    assert(stmt != NULL);

    int (*gen)(const struct ast_node *) = NULL;

    switch (stmt->an) {
    case AST_AN_VOID:
    case AST_AN_TYPE:
    case AST_AN_FUNC:
        assert(0), abort();

    case AST_AN_DECL: gen = gen_decl_auto; break;
    case AST_AN_COND: gen = gen_cond; break;
    case AST_AN_BLOK:
    case AST_AN_NOIN: gen = gen_blok; break;
    case AST_AN_WHIL: gen = gen_whil; break;
    case AST_AN_DOWH: gen = gen_dowh; break;

    case AST_AN_RETN: gen = gen_retn; break;
    case AST_AN_WAIT: gen = gen_wait; break;
    case AST_AN_WLAB: gen = gen_wlab; break;

    case AST_AN_BEXP:
    case AST_AN_UEXP:
    case AST_AN_FEXP:
    case AST_AN_XEXP:
    case AST_AN_AEXP:
    case AST_AN_TEXP:
    case AST_AN_NMBR:
    case AST_AN_STRL:
    case AST_AN_NAME: {
        struct codegen_opd unused;
        GEN_EXPR(stmt, &unused, false);
        INSN(NOP);
    } break;
    }

    const int result = gen ? gen(stmt) : CODEGEN_OK;
    temp_off = 0;
    return result;
}

static int gen_func(const struct ast_node *const node)
{
    assert(node != NULL);
    assert(node->an == AST_AN_FUNC);

    const struct ast_func *const func = ast_data(node, func);
    ftag = htab_get(funcs, (uintptr_t) node);
    ftag->loc = ip;

    const size_t incsp_ip = ip;
    OPD_IMM(addend, 0, ftag->frame_size - ftag->args_size, 8);
    OPD_IMM(tsize, 0, 0, 8);
    INSN_INCSP(addend, tsize);

    temp_off_peak = 0;

    for (size_t idx = 0; idx < func->stmt_count; ++idx) {
        GEN_STMT(func->stmts[idx]);
    }

    o->insns[incsp_ip].incsp.tsize.imm = temp_off_peak;
    OPD_IMM(size, 0, ftag->frame_size + 16, 8);
    INSN_RET(size);
    ftag = NULL;
    return CODEGEN_OK;
}

static const char *op_mnemonic(const codegen_op_t op)
{
    switch (op) {
    case CODEGEN_OP_NOP:   return "nop";

    case CODEGEN_OP_ADD:   return "add";
    case CODEGEN_OP_SUB:   return "sub";
    case CODEGEN_OP_MUL:   return "mul";
    case CODEGEN_OP_DIV:   return "div";
    case CODEGEN_OP_MOD:   return "mod";

    case CODEGEN_OP_EQU:   return "equ";
    case CODEGEN_OP_NEQ:   return "neq";
    case CODEGEN_OP_LT:    return "lt";
    case CODEGEN_OP_GT:    return "gt";
    case CODEGEN_OP_LTE:   return "lte";
    case CODEGEN_OP_GTE:   return "gte";

    case CODEGEN_OP_LSH:   return "lsh";
    case CODEGEN_OP_RSH:   return "rsh";
    case CODEGEN_OP_AND:   return "and";
    case CODEGEN_OP_XOR:   return "xor";
    case CODEGEN_OP_OR:    return "or";

    case CODEGEN_OP_DRF:   return "drf";
    case CODEGEN_OP_RTE:   return "rte";
    case CODEGEN_OP_RTEV:  return "rtev";
    case CODEGEN_OP_QAT:   return "qat";
    case CODEGEN_OP_WAIT:  return "wait";
    case CODEGEN_OP_WLAB:  return "wlab";
    case CODEGEN_OP_GETSP: return "getsp";
    case CODEGEN_OP_QNT:   return "qnt";
    case CODEGEN_OP_QNTV:  return "qntv";

    case CODEGEN_OP_MOV:   return "mov";
    case CODEGEN_OP_CAST:  return "cast";
    case CODEGEN_OP_REF:   return "ref";
    case CODEGEN_OP_NEG:   return "neg";
    case CODEGEN_OP_BNEG:  return "bneg";
    case CODEGEN_OP_NOT:   return "not";
    case CODEGEN_OP_OZ:    return "oz";

    case CODEGEN_OP_JZ:    return "jz";
    case CODEGEN_OP_JNZ:   return "jnz";
    case CODEGEN_OP_JMP:   return "jmp";

    case CODEGEN_OP_INC:   return "inc";
    case CODEGEN_OP_DEC:   return "dec";
    case CODEGEN_OP_INCP:  return "incp";
    case CODEGEN_OP_DECP:  return "decp";

    case CODEGEN_OP_PUSHR: return "pushr";
    case CODEGEN_OP_PUSH:  return "push";
    case CODEGEN_OP_CALL:  return "call";
    case CODEGEN_OP_CALLV: return "callv";
    case CODEGEN_OP_INCSP: return "incsp";
    case CODEGEN_OP_RET:   return "ret";
    case CODEGEN_OP_RETV:  return "retv";

    case CODEGEN_OP_NOINT: return "noint";
    case CODEGEN_OP_INT:   return "int";
    case CODEGEN_OP_BFUN:  return "bfun";

    default: assert(0), abort();
    }
}

static void print_opd(struct codegen_opd *const opd)
{
    printf("%s%s", opd->signd ? "s" : "", opd->indirect ? "*" : "");

    switch (opd->opd) {
    case CODEGEN_OPD_TEMP:
        printf("T[%" PRIu64 ":%" PRIu64 "] ", opd->off, opd->size);
        break;

    case CODEGEN_OPD_AUTO:
        printf("A[%" PRIu64 ":%" PRIu64 "] ", opd->off, opd->size);
        break;

    case CODEGEN_OPD_GLOB:
        printf("G[%" PRIu64 ":%" PRIu64 "] ", opd->off, opd->size);
        break;

    case CODEGEN_OPD_IMM:
        if (!opd->immsize) {
            const struct func_tag *const tag = htab_get(funcs, (uintptr_t) opd->imm);
            opd->imm = tag->loc;
            opd->immsize = 8;
        }

        printf("I[%" PRIu64 ":%" PRIu64 "] ", opd->imm, opd->immsize);
        break;

    default: assert(0), abort();
    }
}

static void print_insns(void)
{
    for (size_t idx = 0; idx < ip; ++idx) {
        struct codegen_insn *const insn = &o->insns[idx];
        printf("%04zu %5s ", idx, op_mnemonic(insn->op));

        switch (o->insns[idx].op) {
        case CODEGEN_OP_ADD:
        case CODEGEN_OP_SUB:
        case CODEGEN_OP_MUL:
        case CODEGEN_OP_DIV:
        case CODEGEN_OP_MOD:

        case CODEGEN_OP_EQU:
        case CODEGEN_OP_NEQ:
        case CODEGEN_OP_LT:
        case CODEGEN_OP_GT:
        case CODEGEN_OP_LTE:
        case CODEGEN_OP_GTE:

        case CODEGEN_OP_LSH:
        case CODEGEN_OP_RSH:
        case CODEGEN_OP_AND:
        case CODEGEN_OP_XOR:
        case CODEGEN_OP_OR:
            print_opd(&insn->bin.dst);
            print_opd(&insn->bin.src1);
            print_opd(&insn->bin.src2);
            break;

        case CODEGEN_OP_DRF:
        case CODEGEN_OP_RTEV:
        case CODEGEN_OP_MOV:
        case CODEGEN_OP_CAST:
        case CODEGEN_OP_REF:
        case CODEGEN_OP_NOT:
        case CODEGEN_OP_NEG:
        case CODEGEN_OP_BNEG:
        case CODEGEN_OP_OZ:
        case CODEGEN_OP_INCP:
        case CODEGEN_OP_DECP:
            print_opd(&insn->un.dst);
            print_opd(&insn->un.src);
            break;

        case CODEGEN_OP_RTE:
            print_opd(&insn->un.src);
            break;

        case CODEGEN_OP_INC:
        case CODEGEN_OP_DEC:
        case CODEGEN_OP_GETSP:
            print_opd(&insn->dst);
            break;

        case CODEGEN_OP_QAT:
            print_opd(&insn->qat.dst);
            print_opd(&insn->qat.quaint);
            printf("%" PRIxPTR ":%" PRIu64, insn->qat.func, insn->qat.wlab_id);
            break;

        case CODEGEN_OP_QNTV:
            print_opd(&insn->qntv.dst);
            print_opd(&insn->qntv.val);
            break;

        case CODEGEN_OP_QNT:
            print_opd(&insn->qnt.dst);
            print_opd(&insn->qnt.loc);
            print_opd(&insn->qnt.sp);
            break;

        case CODEGEN_OP_WAIT:
            print_opd(&insn->wait.quaint);
            print_opd(&insn->wait.timeout);
            printf("%" PRIxPTR ":%" PRIu64, insn->wait.func, insn->wait.wlab_id);
            printf(" %u:%u:%u",
                insn->wait.noblock, insn->wait.units, insn->wait.has_timeout);
            break;

        case CODEGEN_OP_WLAB:
            printf("%" PRIxPTR ":%" PRIu64, insn->wlab.func, insn->wlab.id);
            break;

        case CODEGEN_OP_JZ:
        case CODEGEN_OP_JNZ:
            print_opd(&insn->jmp.cond);
            printf("%04" PRIu64, insn->jmp.loc);
            break;

        case CODEGEN_OP_JMP:
            printf("%04" PRIu64, insn->jmp.loc);
            break;

        case CODEGEN_OP_NOINT:
        case CODEGEN_OP_INT:
        case CODEGEN_OP_NOP:
            break;

        case CODEGEN_OP_BFUN:
            break;

        case CODEGEN_OP_PUSHR:
            print_opd(&insn->push.val);
            print_opd(&insn->push.ssp);
            break;

        case CODEGEN_OP_PUSH:
            print_opd(&insn->push.val);
            break;

        case CODEGEN_OP_CALL:
            print_opd(&insn->call.loc);
            print_opd(&insn->call.bp);
            break;

        case CODEGEN_OP_CALLV:
            print_opd(&insn->call.val);
            print_opd(&insn->call.loc);
            print_opd(&insn->call.bp);
            break;

        case CODEGEN_OP_INCSP:
            print_opd(&insn->incsp.addend);
            print_opd(&insn->incsp.tsize);
            break;

        case CODEGEN_OP_RET:
            print_opd(&insn->ret.size);
            break;

        case CODEGEN_OP_RETV:
            print_opd(&insn->ret.val);
            print_opd(&insn->ret.size);
            break;

        default: assert(0), abort();
        }

        puts("");
    }
}

int codegen_obj_create(const struct ast_node *const root,
    struct codegen_obj *const obj)
{
    assert(root != NULL);

    obj->insns = NULL;
    obj->strings.mem = NULL;
    obj->strings.size = 0;

    o = obj;
    insn_size = strings_mem_size = ip = temp_off = 0;

    size_t decl_count, func_count;
    count_top_decls_and_funcs(root, &decl_count, &func_count);

    for (size_t idx = 0; idx < SCOPE_BFUN_ID_COUNT; ++idx) {
        INSN(BFUN);
    }

    if (create_global_and_frame_layouts(
        root, decl_count, func_count, &o->data_size)) {

        return NOMEM;
    }

    const struct ast_unit *const unit = ast_data(root, unit);
    int error = CODEGEN_OK;

    for (size_t idx = 0; idx < unit->stmt_count; ++idx) {
        const struct ast_node *const stmt = unit->stmts[idx];

        assert(stmt != NULL);
        assert(stmt->an != AST_AN_VOID);

        switch (stmt->an) {
        case AST_AN_TYPE:
        case AST_AN_DECL:
            break;

        case AST_AN_FUNC:
            if (gen_func(stmt)) {
                error = CODEGEN_NOMEM;
                goto out;
            }
            break;

        default: assert(0), abort();
        }
    }

    o->insn_count = ip;
    print_insns();

out:
    htab_destroy(globals, htab_default_dtor);
    htab_destroy(funcs, funcs_dtor);

    return error;
}

void codegen_obj_destroy(const struct codegen_obj *const obj)
{
    if (obj) {
        free(obj->strings.mem);
        free(obj->insns);
    }
}
