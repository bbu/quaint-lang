#include "exec.h"

#include "codegen.h"
#include "scope.h" /* we only need built-in function id's */

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

/* OS X doesn't have clock_gettime(), define a drop-in replacement */
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>

#define CLOCK_MONOTONIC SYSTEM_CLOCK

static int clock_gettime(const clock_id_t clk_id, struct timespec *const ts)
{
    clock_serv_t cclock;

    if (unlikely(host_get_clock_service(mach_host_self(), clk_id, &cclock))) {
        return errno = EINVAL;
    }

    mach_timespec_t mts;

    if (unlikely(clock_get_time(cclock, &mts))) {
        mach_port_deallocate(mach_task_self(), cclock);
        return errno = EINVAL;
    }

    mach_port_deallocate(mach_task_self(), cclock);

    ts->tv_sec = (__darwin_time_t) mts.tv_sec;
    ts->tv_nsec = mts.tv_nsec;

    return 0;
}

#endif

static int get_monotonic_time(uint64_t *const value)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
        return perror("clock_gettime"), -1;
    }

    *value = (uint64_t) ts.tv_sec * (uint64_t) 1000000000 + (uint64_t) ts.tv_nsec;
    return 0;
}

#define LEGAL_IF(cond, msg, ...) \
    if (unlikely(!(cond))) { \
        fprintf(stderr, "%s:%d: illegal instruction at %" PRIu64 ": ", \
            __FILE__, __LINE__, vm->ip); \
        \
        fputs(#cond, stderr); \
        fprintf(stderr, ": " msg "\n", ## __VA_ARGS__); \
        return EXEC_ILLEGAL; \
    }

static uint64_t now;
static uint8_t *bss;
static uint64_t bss_size;

struct tmp_frame {
    struct tmp_frame *prev;
    uint8_t mem[];
};

#define STACK_SIZE (4096 * 4 - sizeof(struct qvm))

struct qvm {
    struct qvm *parent;
    uint64_t ip, sp, bp;
    uint64_t at_start: 1, at_end: 1, noint: 1,
        waiting: 1, waiting_for: 1, waiting_until: 1, waiting_noblock: 1;

    union {
        struct {
            uint64_t start, interval;
        } wait_for;

        struct {
            uintptr_t func;
            uint64_t id;
        } wait_until;
    };

    struct {
        uintptr_t func;
        uint64_t id;
    } last_passed;

    struct tmp_frame *temps;
    uint8_t stack[] __attribute__((aligned(8)));
};

static struct qvm *vm;
static const struct codegen_obj *o;

static uint64_t opd_size(const struct codegen_opd *const operand)
{
    switch (operand->opd) {
    case CODEGEN_OPD_IMM:
        assert(operand->signd == 0);
        assert(operand->indirect == 0);
        assert(powerof2(operand->immsize) && operand->immsize <= 8);
        return operand->immsize;

    case CODEGEN_OPD_TEMP:
    case CODEGEN_OPD_AUTO:
    case CODEGEN_OPD_GLOB:
        assert(operand->size != 0);
        return operand->size;

    default: assert(0), abort();
    }
}

static void *opd_val(const struct codegen_opd *const operand)
{
    void *base;

    switch (operand->opd) {
    case CODEGEN_OPD_IMM:
        return (void *) &operand->imm;

    case CODEGEN_OPD_TEMP: {
        assert(vm->temps != NULL);
        assert(vm->temps->mem != NULL);
        base = vm->temps->mem + operand->off;
    } break;

    case CODEGEN_OPD_AUTO: {
        assert(vm->bp + operand->off < STACK_SIZE);
        base = vm->stack + vm->bp + operand->off;
    } break;

    case CODEGEN_OPD_GLOB: {
        assert(operand->off + (operand->indirect ? 8 : operand->size) <= bss_size);
        base = bss + operand->off;
    } break;

    default: assert(0), abort();
    }

    if (operand->indirect) {
        const uint64_t ptr = *(uint64_t *) base;

        if (ptr == 0) {
            fprintf(stderr, "warn: null pointer dereference\n");
        }

        return (void *) (uintptr_t) ptr;
    } else {
        return base;
    }
}

static int exit_status_from_retval(const struct codegen_insn *const insn)
{
    const uint8_t val_signd = insn->ret.val.signd;
    const uint64_t val_size = opd_size(&insn->ret.val);
    const void *const val = opd_val(&insn->ret.val);

    switch (val_size) {
    case 1: return val_signd ? (int) *( int8_t *) val : (int) *( uint8_t *) val;
    case 2: return val_signd ? (int) *(int16_t *) val : (int) *(uint16_t *) val;
    case 4: return val_signd ? (int) *(int32_t *) val : (int) *(uint32_t *) val;
    case 8: return val_signd ? (int) *(int64_t *) val : (int) *(uint64_t *) val;
    }

    return 0;
}

static void check_and_eventually_split_vms(void)
{
    static uint64_t cycles;

    if (cycles++ % 200 == 0 && get_monotonic_time(&now)) {
        return;
    }

    uint8_t noint = vm->noint;
    struct qvm *old_vm = vm, *current_vm = vm->parent;

    while (current_vm && !noint) {
        bool split = false;

        if (current_vm->waiting) {
            if (current_vm->waiting_for) {
                split = now - current_vm->wait_for.start >= current_vm->wait_for.interval;
            } else if (current_vm->waiting_until) {
                split = old_vm->last_passed.func == current_vm->wait_until.func &&
                    old_vm->last_passed.id == current_vm->wait_until.id;
            }
        }

        if (split) {
            vm = current_vm;
            vm->waiting = 0;
            vm->waiting_for = 0;
            vm->waiting_until = 0;
            vm->waiting_noblock = 0;
            vm->ip++;
            return;
        }

        noint = current_vm->noint;
        old_vm = current_vm;
        current_vm = current_vm->parent;
    }
}

static void cleanup_temps(void **const temps)
{
    free(*temps);
}

static int handle_return(const struct codegen_insn *insn,
    const uint64_t retval_size, const void *const retval)
{
    void *old_temps __attribute__((__cleanup__(cleanup_temps))) = vm->temps;
    (void) old_temps;

    if (vm->sp == 0 && vm->parent) {
        insn = &o->insns[vm->parent->ip];

        LEGAL_IF(insn->op == (retval_size ? CODEGEN_OP_RTEV : CODEGEN_OP_RTE) ||
            insn->op == CODEGEN_OP_WAIT, "");

        switch (insn->op) {
        case CODEGEN_OP_RTEV: {
            struct qvm *const old_vm = vm;
            vm = vm->parent;
            const uint64_t cval_size = opd_size(&insn->un.dst);
            void *const cval = opd_val(&insn->un.dst);

            LEGAL_IF(retval_size == cval_size, "%" PRIu64 ", %" PRIu64,
                retval_size, cval_size);

            memcpy(cval, retval, (size_t) cval_size);
            free(old_vm);
            *(uint64_t *) opd_val(&insn->un.src) = 0;
        } break;

        case CODEGEN_OP_RTE: {
            struct qvm *const old_vm = vm;
            vm = vm->parent;
            free(old_vm);
            *(uint64_t *) opd_val(&insn->un.src) = 0;
        } break;

        case CODEGEN_OP_WAIT: {
            vm->at_end = 1;
            vm->last_passed.func = 0;
            vm->last_passed.id = 0;

            if (retval_size) {
                memcpy(vm->stack, retval, (size_t) retval_size);
            }

            vm->temps = NULL;
            vm = vm->parent;
            vm->waiting = 0;
            vm->waiting_for = 0;
            vm->waiting_until = 0;
            vm->waiting_noblock = 0;
        } break;
        }

        vm->ip++;
    } else if (vm->sp == 0) {
        const int status = retval_size ? exit_status_from_retval(insn) : 0;
        vm->temps = NULL;
        vm->ip = *(uint64_t *) (vm->stack + vm->sp);
        vm->bp = *(uint64_t *) (vm->stack + vm->sp + 8);
        return status;
    } else {
        vm->temps = vm->temps->prev;
        vm->ip = *(uint64_t *) (vm->stack + vm->sp);
        vm->bp = *(uint64_t *) (vm->stack + vm->sp + 8);

        LEGAL_IF(vm->ip < o->insn_count, "%" PRIu64, vm->ip);
        LEGAL_IF(vm->bp <= STACK_SIZE, "%" PRIu64, vm->bp);

        if (retval_size) {
            insn = &o->insns[vm->ip];
            LEGAL_IF(insn->op == CODEGEN_OP_CALLV, "");
            const uint64_t cval_size = opd_size(&insn->call.val);

            LEGAL_IF(retval_size == cval_size, "%" PRIu64 ", %" PRIu64,
                retval_size, cval_size);

            void *const cval = opd_val(&insn->call.val);
            memcpy(cval, retval, (size_t) cval_size);
        }

        vm->ip++;
    }

    return EXEC_OK;
}

static int insn_mov(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_MOV);

    const uint64_t dst_size = opd_size(&insn->un.dst);
    const uint64_t src_size = opd_size(&insn->un.src);

    LEGAL_IF(dst_size == src_size, "%" PRIu64 ", %" PRIu64, dst_size, src_size);

    void *const dst = opd_val(&insn->un.dst);
    const void *const src = opd_val(&insn->un.src);

    memcpy(dst, src, (size_t) dst_size);
    return EXEC_OK;
}

static int insn_cast(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_CAST);

    const uint64_t dst_size = opd_size(&insn->un.dst);
    const uint64_t src_size = opd_size(&insn->un.src);

    void *const dst = opd_val(&insn->un.dst);
    const void *const src = opd_val(&insn->un.src);

    memset(dst, 0, (size_t) dst_size);
    memcpy(dst, src, src_size < dst_size ? (size_t) src_size : (size_t) dst_size);
    return EXEC_OK;
}

static int insn_bin_arith(const struct codegen_insn *const insn)
{
    LEGAL_IF(insn->bin.dst.signd == insn->bin.src1.signd &&
        insn->bin.src1.signd == insn->bin.src2.signd, "differing signedness");

    const uint8_t signd = insn->bin.dst.signd;
    const uint64_t dst_size = opd_size(&insn->bin.dst);
    const uint64_t src1_size = opd_size(&insn->bin.src1);
    const uint64_t src2_size = opd_size(&insn->bin.src2);

    LEGAL_IF(powerof2(dst_size) && dst_size <= 8, "%" PRIu64, dst_size);
    LEGAL_IF(powerof2(src1_size) && src1_size <= 8, "%" PRIu64, src1_size);
    LEGAL_IF(powerof2(src2_size) && src2_size <= 8, "%" PRIu64, src2_size);
    LEGAL_IF(dst_size == src1_size && src1_size == src2_size, "differing sizes");

    void *const dst = opd_val(&insn->bin.dst);
    const void *const src1 = opd_val(&insn->bin.src1);
    const void *const src2 = opd_val(&insn->bin.src2);

    #define ARITH_OP(op) \
    switch (dst_size) { \
    case 1: \
        if (signd) { \
            *(int8_t *) dst = (int8_t) (*(int8_t *) src1 op *(int8_t *) src2); \
        } else { \
            *(uint8_t *) dst = (uint8_t) (*(uint8_t *) src1 op *(uint8_t *) src2); \
        } \
        break; \
    \
    case 2: \
        if (signd) { \
            *(int16_t *) dst = (int16_t) (*(int16_t *) src1 op *(int16_t *) src2); \
        } else { \
            *(uint16_t *) dst = (uint16_t) (*(uint16_t *) src1 op *(uint16_t *) src2); \
        } \
        break; \
    \
    case 4: \
        if (signd) { \
            *(int32_t *) dst = *(int32_t *) src1 op *(int32_t *) src2; \
        } else { \
            *(uint32_t *) dst = *(uint32_t *) src1 op *(uint32_t *) src2; \
        } \
        break; \
    \
    case 8: \
        if (signd) { \
            *(int64_t *) dst = *(int64_t *) src1 op *(int64_t *) src2; \
        } else { \
            *(uint64_t *) dst = *(uint64_t *) src1 op *(uint64_t *) src2; \
        } \
        break; \
    }

    switch (insn->op) {
    case CODEGEN_OP_ADD: ARITH_OP( +); break;
    case CODEGEN_OP_SUB: ARITH_OP( -); break;
    case CODEGEN_OP_MUL: ARITH_OP( *); break;
    case CODEGEN_OP_DIV: ARITH_OP( /); break;
    case CODEGEN_OP_MOD: ARITH_OP( %); break;
    case CODEGEN_OP_LSH: ARITH_OP(<<); break;
    case CODEGEN_OP_RSH: ARITH_OP(>>); break;
    case CODEGEN_OP_AND: ARITH_OP( &); break;
    case CODEGEN_OP_XOR: ARITH_OP( ^); break;
    case CODEGEN_OP_OR:  ARITH_OP( |); break;
    default: assert(0), abort();
    }

    #undef ARITH_OP
    return EXEC_OK;
}

static int insn_equ_neq(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_EQU || insn->op == CODEGEN_OP_NEQ);

    const uint8_t dst_signd = insn->bin.dst.signd;
    const uint64_t dst_size = opd_size(&insn->bin.dst);
    const uint64_t src1_size = opd_size(&insn->bin.src1);
    const uint64_t src2_size = opd_size(&insn->bin.src2);

    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(dst_size == 1, "%" PRIu64, dst_size);
    LEGAL_IF(src1_size == src2_size, "%" PRIu64 ", %" PRIu64, src1_size, src2_size);

    uint8_t *const dst = opd_val(&insn->bin.dst);
    const void *const src1 = opd_val(&insn->bin.src1);
    const void *const src2 = opd_val(&insn->bin.src2);

    const int equal = !memcmp(src1, src2, (size_t) src1_size);
    *dst = insn->op == CODEGEN_OP_EQU ? (equal ? 1 : 0) : !equal;
    return EXEC_OK;
}

static int insn_bin_logic(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_LT || insn->op == CODEGEN_OP_GT ||
        insn->op == CODEGEN_OP_LTE || insn->op == CODEGEN_OP_GTE);

    const uint8_t dst_signd = insn->bin.dst.signd;
    const uint64_t dst_size = opd_size(&insn->bin.dst);

    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(dst_size == 1, "%" PRIu64, dst_size);

    const uint8_t src1_signd = insn->bin.src1.signd;
    const uint64_t src1_size = opd_size(&insn->bin.src1);
    const uint8_t src2_signd = insn->bin.src2.signd;
    const uint64_t src2_size = opd_size(&insn->bin.src2);

    LEGAL_IF(src1_signd == src2_signd, "differing signedness");
    LEGAL_IF(powerof2(src1_size) && src1_size <= 8, "%" PRIu64, src1_size);
    LEGAL_IF(powerof2(src2_size) && src2_size <= 8, "%" PRIu64, src2_size);
    LEGAL_IF(src1_size == src2_size, "differing sizes");

    uint8_t *const dst = opd_val(&insn->bin.dst);
    const void *const src1 = opd_val(&insn->bin.src1);
    const void *const src2 = opd_val(&insn->bin.src2);

    #define LOGIC_OP(op) \
    switch (src1_size) { \
    case 1: \
        if (src1_signd) { \
            *dst = *(int8_t *) src1 op *(int8_t *) src2; \
        } else { \
            *dst = *(uint8_t *) src1 op *(uint8_t *) src2; \
        } \
        break; \
    \
    case 2: \
        if (src1_signd) { \
            *dst = *(int16_t *) src1 op *(int16_t *) src2; \
        } else { \
            *dst = *(uint16_t *) src1 op *(uint16_t *) src2; \
        } \
        break; \
    \
    case 4: \
        if (src1_signd) { \
            *dst = *(int32_t *) src1 op *(int32_t *) src2; \
        } else { \
            *dst = *(uint32_t *) src1 op *(uint32_t *) src2; \
        } \
        break; \
    \
    case 8: \
        if (src1_signd) { \
            *dst = *(int64_t *) src1 op *(int64_t *) src2; \
        } else { \
            *dst = *(uint64_t *) src1 op *(uint64_t *) src2; \
        } \
        break; \
    }

    switch (insn->op) {
    case CODEGEN_OP_LT:  LOGIC_OP( <); break;
    case CODEGEN_OP_GT:  LOGIC_OP( >); break;
    case CODEGEN_OP_LTE: LOGIC_OP(<=); break;
    case CODEGEN_OP_GTE: LOGIC_OP(>=); break;
    default: assert(0), abort();
    }

    #undef LOGIC_OP
    return EXEC_OK;
}

static int insn_not(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_NOT);

    const uint8_t dst_signd = insn->un.dst.signd;
    const uint64_t dst_size = opd_size(&insn->un.dst);

    LEGAL_IF(dst_signd == 0 || dst_signd == 1, "%u", dst_signd);
    LEGAL_IF(powerof2(dst_size) && dst_size <= 8, "%" PRIu64, dst_size);

    const uint8_t src_signd = insn->un.src.signd;
    const uint64_t src_size = opd_size(&insn->un.src);

    LEGAL_IF(src_signd == dst_signd, "%u, %u", src_signd, dst_signd);
    LEGAL_IF(powerof2(src_size) && src_size <= 8, "%" PRIu64, src_size);
    LEGAL_IF(dst_size == src_size, "%" PRIu64 ", %" PRIu64, dst_size, src_size);

    void *const dst = opd_val(&insn->un.dst);
    const void *const src = opd_val(&insn->un.src);

    switch (dst_size) {
    case 1:
        if (dst_signd) {
            *(int8_t *) dst = !*(int8_t *) src;
        } else {
            *(uint8_t *) dst = !*(uint8_t *) src;
        }
        break;

    case 2:
        if (dst_signd) {
            *(int16_t *) dst = !*(int16_t *) src;
        } else {
            *(uint16_t *) dst = !*(uint16_t *) src;
        }
        break;

    case 4:
        if (dst_signd) {
            *(int32_t *) dst = !*(int32_t *) src;
        } else {
            *(uint32_t *) dst = !*(uint32_t *) src;
        }
        break;

    case 8:
        if (dst_signd) {
            *(int64_t *) dst = !*(int64_t *) src;
        } else {
            *(uint64_t *) dst = !*(uint64_t *) src;
        }
        break;
    }

    return EXEC_OK;
}

static int insn_neg(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_NEG);

    const uint8_t dst_signd = insn->un.dst.signd;
    const uint64_t dst_size = opd_size(&insn->un.dst);

    LEGAL_IF(dst_signd == 1, "%u", dst_signd);
    LEGAL_IF(powerof2(dst_size) && dst_size <= 8, "%" PRIu64, dst_size);

    const uint8_t src_signd = insn->un.src.signd;
    const uint64_t src_size = opd_size(&insn->un.src);

    LEGAL_IF(src_signd == 0 || src_signd == 1, "%u", src_signd);
    LEGAL_IF(powerof2(src_size) && src_size <= 8, "%" PRIu64, src_size);
    LEGAL_IF(dst_size == src_size, "%" PRIu64 ", %" PRIu64, dst_size, src_size);

    void *const dst = opd_val(&insn->un.dst);
    const void *const src = opd_val(&insn->un.src);

    switch (dst_size) {
    case 1:
        if (src_signd) {
            *(int8_t *) dst = (int8_t) -*(int8_t *) src;
        } else {
            *(int8_t *) dst = (int8_t) -*(uint8_t *) src;
        }
        break;

    case 2:
        if (src_signd) {
            *(int16_t *) dst = (int16_t) -*(int16_t *) src;
        } else {
            *(int16_t *) dst = (int16_t) -*(uint16_t *) src;
        }
        break;

    case 4:
        if (src_signd) {
            *(int32_t *) dst = -*(int32_t *) src;
        } else {
            *(int32_t *) dst = (int32_t) -*(uint32_t *) src;
        }
        break;

    case 8:
        if (src_signd) {
            *(int64_t *) dst = -*(int64_t *) src;
        } else {
            *(int64_t *) dst = (int64_t) -*(uint64_t *) src;
        }
        break;
    }

    return EXEC_OK;
}

static int insn_bneg(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_BNEG);

    const uint8_t dst_signd = insn->un.dst.signd;
    const uint64_t dst_size = opd_size(&insn->un.dst);

    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(powerof2(dst_size) && dst_size <= 8, "%" PRIu64, dst_size);

    const uint8_t src_signd = insn->un.src.signd;
    const uint64_t src_size = opd_size(&insn->un.src);

    LEGAL_IF(src_signd == 0, "%u", src_signd);
    LEGAL_IF(powerof2(src_size) && src_size <= 8, "%" PRIu64, src_size);
    LEGAL_IF(dst_size == src_size, "%" PRIu64 ", %" PRIu64, dst_size, src_size);

    void *const dst = opd_val(&insn->un.dst);
    const void *const src = opd_val(&insn->un.src);

    switch (dst_size) {
    case 1:
        *(uint8_t *) dst = ~*(uint8_t *) src;
        break;

    case 2:
        *(uint16_t *) dst = ~*(uint16_t *) src;
        break;

    case 4:
        *(uint32_t *) dst = ~*(uint32_t *) src;
        break;

    case 8:
        *(uint64_t *) dst = ~*(uint64_t *) src;
        break;
    }

    return EXEC_OK;
}

static int insn_oz(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_OZ);

    const uint8_t dst_signd = insn->un.dst.signd;
    const uint64_t dst_size = opd_size(&insn->un.dst);

    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(dst_size == 1, "%" PRIu64, dst_size);

    const uint8_t src_signd = insn->un.src.signd;
    const uint64_t src_size = opd_size(&insn->un.src);

    LEGAL_IF(src_signd == 0 || src_signd == 1, "%u", src_signd);
    LEGAL_IF(powerof2(src_size) && src_size <= 8, "%" PRIu64, src_size);

    uint8_t *const dst = opd_val(&insn->un.dst);
    const void *const src = opd_val(&insn->un.src);

    switch (src_size) {
    case 1:
        *dst = src_signd ? (*(int8_t *) src ? 1 : 0) : (*(uint8_t *) src ? 1 : 0);
        break;

    case 2:
        *dst = src_signd ? (*(int16_t *) src ? 1 : 0) : (*(uint16_t *) src ? 1 : 0);
        break;

    case 4:
        *dst = src_signd ? (*(int32_t *) src ? 1 : 0) : (*(uint32_t *) src ? 1 : 0);
        break;

    case 8:
        *dst = src_signd ? (*(int64_t *) src ? 1 : 0) : (*(uint64_t *) src ? 1 : 0);
        break;
    }

    return EXEC_OK;
}

static int insn_inc_dec(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_INC || insn->op == CODEGEN_OP_DEC);

    const uint8_t dst_signd = insn->dst.signd;
    const uint64_t dst_size = opd_size(&insn->dst);

    LEGAL_IF(dst_signd == 0 || dst_signd == 1, "%u", dst_signd);
    LEGAL_IF(powerof2(dst_size) && dst_size <= 8, "%" PRIu64, dst_size);

    void *const dst = opd_val(&insn->dst);

    #define INC_DEC_OP(op) \
    switch (dst_size) { \
    case 1: \
        if (dst_signd) { \
            (*(int8_t *) dst)op; \
        } else { \
            (*(uint8_t *) dst)op; \
        } \
        break; \
    \
    case 2: \
        if (dst_signd) { \
            (*(int16_t *) dst)op; \
        } else { \
            (*(uint16_t *) dst)op; \
        } \
        break; \
    \
    case 4: \
        if (dst_signd) { \
            (*(int32_t *) dst)op; \
        } else { \
            (*(uint32_t *) dst)op; \
        } \
        break; \
    \
    case 8: \
        if (dst_signd) { \
            (*(int64_t *) dst)op; \
        } else { \
            (*(uint64_t *) dst)op; \
        } \
        break; \
    }

    if (insn->op == CODEGEN_OP_INC) {
        INC_DEC_OP(++);
    } else {
        INC_DEC_OP(--);
    }

    #undef INC_DEC_OP
    return EXEC_OK;
}

static int insn_incp_decp(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_INCP || insn->op == CODEGEN_OP_DECP);

    const uint8_t dst_signd = insn->un.dst.signd;
    const uint64_t dst_size = opd_size(&insn->un.dst);

    LEGAL_IF(dst_signd == 0 || dst_signd == 1, "%u", dst_signd);
    LEGAL_IF(powerof2(dst_size) && dst_size <= 8, "%" PRIu64, dst_size);

    const uint8_t src_signd = insn->un.src.signd;
    const uint64_t src_size = opd_size(&insn->un.src);

    LEGAL_IF(src_signd == dst_signd, "%u, %u", src_signd, dst_signd);
    LEGAL_IF(powerof2(src_size) && src_size <= 8, "%" PRIu64, src_size);
    LEGAL_IF(dst_size == src_size, "%" PRIu64 ", %" PRIu64, dst_size, src_size);

    void *const dst = opd_val(&insn->un.dst);
    void *const src = opd_val(&insn->un.src);

    #define INCP_DECP_OP(op) \
    switch (dst_size) { \
    case 1: \
        if (dst_signd) { \
            *(int8_t *) dst = (*(int8_t *) src)op; \
        } else { \
            *(uint8_t *) dst = (*(uint8_t *) src)op; \
        } \
        break; \
    \
    case 2: \
        if (dst_signd) { \
            *(int16_t *) dst = (*(int16_t *) src)op; \
        } else { \
            *(uint16_t *) dst = (*(uint16_t *) src)op; \
        } \
        break; \
    \
    case 4: \
        if (dst_signd) { \
            *(int32_t *) dst = (*(int32_t *) src)op; \
        } else { \
            *(uint32_t *) dst = (*(uint32_t *) src)op; \
        } \
        break; \
    \
    case 8: \
        if (dst_signd) { \
            *(int64_t *) dst = (*(int64_t *) src)op; \
        } else { \
            *(uint64_t *) dst = (*(uint64_t *) src)op; \
        } \
        break; \
    }

    if (insn->op == CODEGEN_OP_INCP) {
        INCP_DECP_OP(++);
    } else {
        INCP_DECP_OP(--);
    }

    #undef INCP_DECP_OP
    return EXEC_OK;
}

static int insn_cjmp(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_JZ || insn->op == CODEGEN_OP_JNZ);

    const uint8_t cond_signd = insn->jmp.cond.signd;
    const uint64_t cond_size = opd_size(&insn->jmp.cond);

    LEGAL_IF(cond_signd == 0 || cond_signd == 1, "%u", cond_signd);
    LEGAL_IF(cond_size > 0, "0");

    const void *const cond = opd_val(&insn->jmp.cond);
    static const uint8_t zero_mem[128];
    int all_zero;

    if (unlikely(cond_size > 128)) {
        void *const zero_alloc = calloc(1, (size_t) cond_size);

        if (unlikely(!zero_alloc)) {
            return EXEC_NOMEM;
        }

        all_zero = !memcmp(zero_alloc, cond, (size_t) cond_size);
        free(zero_alloc);
    } else {
        all_zero = !memcmp(zero_mem, cond, (size_t) cond_size);
    }

    if (insn->op == CODEGEN_OP_JZ) {
        if (all_zero) {
            vm->ip = insn->jmp.loc;
        } else {
            vm->ip++;
        }
    } else {
        if (all_zero) {
            vm->ip++;
        } else {
            vm->ip = insn->jmp.loc;
        }
    }

    return EXEC_OK;
}

static int insn_jmp(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_JMP);
    vm->ip = insn->jmp.loc;
    return EXEC_OK;
}

static int insn_pushr(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_PUSHR);

    const uint8_t retip_is_imm = insn->push.val.opd == CODEGEN_OPD_IMM;
    const uint8_t retip_signd = insn->push.val.signd;
    const uint8_t retip_indirect = insn->push.val.indirect;
    const uint64_t retip_size = opd_size(&insn->push.val);

    LEGAL_IF(retip_is_imm == 1, "%u", retip_is_imm);
    LEGAL_IF(retip_signd == 0, "%u", retip_signd);
    LEGAL_IF(retip_indirect == 0, "%u", retip_indirect);
    LEGAL_IF(retip_size == 8, "%" PRIu64, retip_size);

    const uint8_t ssp_is_temp = insn->push.ssp.opd == CODEGEN_OPD_TEMP;
    const uint8_t ssp_signd = insn->push.ssp.signd;
    const uint8_t ssp_indirect = insn->push.ssp.indirect;
    const uint64_t ssp_size = opd_size(&insn->push.ssp);

    LEGAL_IF(ssp_is_temp == 1, "%u", ssp_is_temp);
    LEGAL_IF(ssp_signd == 0, "%u", ssp_signd);
    LEGAL_IF(ssp_indirect == 0, "%u", ssp_indirect);
    LEGAL_IF(ssp_size == 8, "%" PRIu64, ssp_size);

    LEGAL_IF(vm->sp % 8 == 0, "%" PRIu64, vm->sp);
    LEGAL_IF(vm->sp + 16 <= STACK_SIZE, "%" PRIu64, vm->sp);

    const uint64_t retip = insn->push.val.imm;
    uint64_t *const ssp = opd_val(&insn->push.ssp);

    *(uint64_t *) (vm->stack + vm->sp) = retip;
    *(uint64_t *) (vm->stack + vm->sp + 8) = (uint64_t) vm->bp;

    vm->sp += 16;
    *ssp = vm->sp;

    return EXEC_OK;
}

static int insn_push(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_PUSH);

    const uint8_t val_signd = insn->push.val.signd;
    const uint8_t val_indirect = insn->push.val.indirect;
    const uint64_t val_size = opd_size(&insn->push.val);

    LEGAL_IF(val_signd == 0 || val_signd == 1, "%u", val_signd);
    LEGAL_IF(val_indirect == 0 || val_indirect == 1, "%u", val_indirect);
    LEGAL_IF(val_size > 0, "%" PRIu64, val_size);

    LEGAL_IF(vm->sp % 8 == 0, "%" PRIu64, vm->sp);
    LEGAL_IF(vm->sp + val_size <= STACK_SIZE, "%" PRIu64 ", %" PRIu64, vm->sp, val_size);

    const void *const val = opd_val(&insn->push.val);
    memcpy(vm->stack + vm->sp, val, (size_t) val_size);
    vm->sp += val_size;
    ALIGN_UP(vm->sp, 8);
    LEGAL_IF(vm->sp <= STACK_SIZE, "%" PRIu64, vm->sp);

    return EXEC_OK;
}

static int insn_call_callv(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_CALL || insn->op == CODEGEN_OP_CALLV);

    const uint8_t loc_signd = insn->call.loc.signd;
    const uint8_t loc_indirect = insn->call.loc.indirect;
    const uint64_t loc_size = opd_size(&insn->call.loc);

    LEGAL_IF(loc_signd == 0, "%u", loc_signd);
    LEGAL_IF(loc_indirect == 0 || loc_indirect == 1, "%u", loc_indirect);
    LEGAL_IF(loc_size == 8, "%" PRIu64, loc_size);

    const uint8_t bp_is_temp = insn->call.bp.opd == CODEGEN_OPD_TEMP;
    const uint8_t bp_signd = insn->call.bp.signd;
    const uint8_t bp_indirect = insn->call.bp.indirect;
    const uint64_t bp_size = opd_size(&insn->call.bp);

    LEGAL_IF(bp_is_temp == 1, "%u", bp_is_temp);
    LEGAL_IF(bp_signd == 0, "%u", bp_signd);
    LEGAL_IF(bp_indirect == 0, "%u", bp_indirect);
    LEGAL_IF(bp_size == 8, "%" PRIu64, bp_size);

    const uint64_t *const loc = opd_val(&insn->call.loc);
    const uint64_t *const bp = opd_val(&insn->call.bp);

    vm->ip = *loc;
    vm->bp = *bp;

    return EXEC_OK;
}

static int insn_incsp(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_INCSP);

    const uint8_t addend_is_imm = insn->incsp.addend.opd == CODEGEN_OPD_IMM;
    const uint8_t addend_signd = insn->incsp.addend.signd;
    const uint8_t addend_indirect = insn->incsp.addend.indirect;
    const uint64_t addend_size = opd_size(&insn->incsp.addend);

    LEGAL_IF(addend_is_imm == 1, "%u", addend_is_imm);
    LEGAL_IF(addend_signd == 0, "%u", addend_signd);
    LEGAL_IF(addend_indirect == 0, "%u", addend_indirect);
    LEGAL_IF(addend_size == 8, "%" PRIu64, addend_size);

    const uint8_t tsize_is_imm = insn->incsp.tsize.opd == CODEGEN_OPD_IMM;
    const uint8_t tsize_signd = insn->incsp.tsize.signd;
    const uint8_t tsize_indirect = insn->incsp.tsize.indirect;
    const uint64_t tsize_size = opd_size(&insn->incsp.tsize);

    LEGAL_IF(tsize_is_imm == 1, "%u", tsize_is_imm);
    LEGAL_IF(tsize_signd == 0, "%u", tsize_signd);
    LEGAL_IF(tsize_indirect == 0, "%u", tsize_indirect);
    LEGAL_IF(tsize_size == 8, "%" PRIu64, tsize_size);

    const uint64_t addend = insn->incsp.addend.imm;
    const uint64_t tsize = insn->incsp.tsize.imm;

    LEGAL_IF(addend % 8 == 0, "%" PRIu64, addend);

    vm->at_start = 0;
    vm->sp += addend;
    LEGAL_IF(vm->sp <= STACK_SIZE, "%" PRIu64, vm->sp);
    LEGAL_IF(vm->sp % 8 == 0, "%" PRIu64, vm->sp);

    struct tmp_frame *const new_tmp_frame =
        malloc(sizeof(struct tmp_frame) + (size_t) tsize);

    if (unlikely(!new_tmp_frame)) {
        return EXEC_NOMEM;
    }

    new_tmp_frame->prev = vm->temps;
    vm->temps = new_tmp_frame;
    return EXEC_OK;
}

static int insn_ret_retv(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_RET || insn->op == CODEGEN_OP_RETV);

    LEGAL_IF(vm->sp % 8 == 0, "%" PRIu64, vm->sp);
    LEGAL_IF(vm->bp % 8 == 0, "%" PRIu64, vm->bp);

    const uint8_t size_is_imm = insn->ret.size.opd == CODEGEN_OPD_IMM;
    const uint8_t size_signd = insn->ret.size.signd;
    const uint8_t size_indirect = insn->ret.size.indirect;
    const uint64_t size_size = opd_size(&insn->ret.size);

    LEGAL_IF(size_is_imm == 1, "%u", size_is_imm);
    LEGAL_IF(size_signd == 0, "%u", size_signd);
    LEGAL_IF(size_indirect == 0, "%u", size_indirect);
    LEGAL_IF(size_size == 8, "%" PRIu64, size_size);

    const uint64_t size = insn->ret.size.imm;

    LEGAL_IF(vm->sp >= size, "%" PRIu64 ", %" PRIu64, vm->sp, size);
    LEGAL_IF(vm->temps != NULL, "");
    vm->sp -= size;

    const bool with_value = insn->op == CODEGEN_OP_RETV;
    const uint64_t retval_size = with_value ? opd_size(&insn->ret.val) : 0;
    const void *const retval = with_value ? opd_val(&insn->ret.val) : NULL;

    return handle_return(insn, retval_size, retval);
}

static int insn_ref(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_REF);

    const uint8_t dst_signd = insn->un.dst.signd;
    const uint8_t dst_indirect = insn->un.dst.indirect;
    const uint64_t dst_size = opd_size(&insn->un.dst);

    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(dst_indirect == 0, "%u", dst_indirect);
    LEGAL_IF(dst_size == 8, "%" PRIu64, dst_size);

    uint64_t *const dst = opd_val(&insn->un.dst);
    *dst = (uint64_t) (uintptr_t) opd_val(&insn->un.src);
    return EXEC_OK;
}

static int insn_drf(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_DRF);

    const uint8_t dst_signd = insn->un.dst.signd;
    const uint8_t dst_indirect = insn->un.dst.indirect;
    const uint64_t dst_size = opd_size(&insn->un.dst);

    LEGAL_IF(dst_signd == 0 || dst_signd == 1, "%u", dst_signd);
    LEGAL_IF(dst_indirect == 0, "%u", dst_indirect);
    LEGAL_IF(dst_size > 0, "0");

    const uint64_t src_size = opd_size(&insn->un.src);

    LEGAL_IF(src_size == 8, "%" PRIu64, src_size);

    void *const dst = opd_val(&insn->un.dst);
    const uint64_t *const src = opd_val(&insn->un.src);

    memcpy(dst, (const void *) (uintptr_t) *src, (size_t) dst_size);
    return EXEC_OK;
}

static int insn_rte_rtev(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_RTE || insn->op == CODEGEN_OP_RTEV);
    const bool with_value = insn->op == CODEGEN_OP_RTEV;

    uint8_t dst_signd;
    uint8_t dst_indirect;
    uint64_t dst_size;
    void *dst;

    if (with_value) {
        dst_signd = insn->un.dst.signd;
        dst_indirect = insn->un.dst.indirect;
        dst_size = opd_size(&insn->un.dst);

        LEGAL_IF(dst_signd == 0 || dst_signd == 1, "%u", dst_signd);
        LEGAL_IF(dst_indirect == 0, "%u", dst_indirect);
        LEGAL_IF(dst_size > 0, "%" PRIu64, dst_size);

        dst = opd_val(&insn->un.dst);
    }

    const uint8_t qnt_signd = insn->un.src.signd;
    const uint8_t qnt_indirect = insn->un.src.indirect;
    const uint64_t qnt_size = opd_size(&insn->un.src);

    LEGAL_IF(qnt_signd == 0, "%u", qnt_signd);
    LEGAL_IF(qnt_indirect == 0 || qnt_indirect == 1, "%u", qnt_indirect);
    LEGAL_IF(qnt_size == 8, "%" PRIu64, qnt_size);

    struct qvm *const qvm = (struct qvm *) (uintptr_t)
        *(uint64_t *) opd_val(&insn->un.src);

    if (!qvm) {
        if (with_value) {
            memset(dst, 0, (size_t) dst_size);
        }

        return vm->ip++, EXEC_OK;
    }

    if (qvm->at_end) {
        if (with_value) {
            memcpy(dst, qvm->stack, (size_t) dst_size);
        }

        free(qvm);
        *(uint64_t *) opd_val(&insn->un.src) = 0;
        return vm->ip++, EXEC_OK;
    }

    qvm->parent = vm;
    vm = qvm;
    return EXEC_OK;
}

static int insn_qat(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_QAT);

    const uint8_t dst_signd = insn->qat.dst.signd;
    const uint8_t dst_indirect = insn->qat.dst.indirect;
    const uint64_t dst_size = opd_size(&insn->qat.dst);

    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(dst_indirect == 0, "%u", dst_indirect);
    LEGAL_IF(dst_size == 1, "%" PRIu64, dst_size);

    const uint8_t qnt_signd = insn->qat.quaint.signd;
    const uint8_t qnt_indirect = insn->qat.quaint.indirect;
    const uint64_t qnt_size = opd_size(&insn->qat.quaint);

    LEGAL_IF(qnt_signd == 0, "%u", qnt_signd);
    LEGAL_IF(qnt_indirect == 0 || qnt_indirect == 1, "%u", qnt_indirect);
    LEGAL_IF(qnt_size == 8, "%" PRIu64, qnt_size);

    uint8_t *const dst = opd_val(&insn->qat.dst);
    struct qvm *const qvm = (struct qvm *) (uintptr_t)
        *(uint64_t *) opd_val(&insn->qat.quaint);

    if (!qvm) {
        *dst = 0;
    } else if (!insn->qat.func && !insn->qat.wlab_id) {
        *dst = qvm->at_start;
    } else if (insn->qat.func == 1) {
        *dst = qvm->at_end;
    } else {
        *dst = qvm->last_passed.func == insn->qat.func &&
            qvm->last_passed.id == insn->qat.wlab_id;
    }

    return EXEC_OK;
}

static int insn_wait(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_WAIT);

    const uint8_t qnt_signd = insn->wait.quaint.signd;
    const uint8_t qnt_indirect = insn->wait.quaint.indirect;
    const uint64_t qnt_size = opd_size(&insn->wait.quaint);

    LEGAL_IF(qnt_signd == 0, "%u", qnt_signd);
    LEGAL_IF(qnt_indirect == 0 || qnt_indirect == 1, "%u", qnt_indirect);
    LEGAL_IF(qnt_size == 8, "%" PRIu64, qnt_size);

    struct qvm *const qvm = (struct qvm *) (uintptr_t)
        *(uint64_t *) opd_val(&insn->wait.quaint);

    if (!qvm || qvm->at_end) {
        return vm->ip++, EXEC_OK;
    }

    uint64_t tm_val;

    if (insn->wait.has_timeout) {
        const uint8_t tm_signd = insn->wait.timeout.signd;
        const uint8_t tm_indirect = insn->wait.timeout.indirect;
        const uint64_t tm_size = opd_size(&insn->wait.timeout);

        LEGAL_IF(tm_signd == 0, "%u", tm_signd);
        LEGAL_IF(tm_indirect == 0 || tm_indirect == 1, "%u", tm_indirect);
        LEGAL_IF(powerof2(tm_size) && tm_size <= 8, "%" PRIu64, tm_size);

        const void *const tm = opd_val(&insn->wait.timeout);

        switch (tm_size) {
        case 1: tm_val = *( uint8_t *) tm; break;
        case 2: tm_val = *(uint16_t *) tm; break;
        case 4: tm_val = *(uint32_t *) tm; break;
        case 8: tm_val = *(uint64_t *) tm; break;
        }

        if (!tm_val) {
            return vm->ip++, EXEC_OK;
        }

        tm_val *= insn->wait.units ? (uint64_t) 1000000000 : (uint64_t) 1000000;
    }

    vm->waiting = 1;
    vm->waiting_for = vm->waiting_until = 0;
    vm->waiting_noblock = insn->wait.noblock;

    if (insn->wait.has_timeout) {
        vm->waiting_for = 1;

        if (unlikely(get_monotonic_time(&now))) {
            return EXEC_NOMEM;
        }

        vm->wait_for.start = now;
        vm->wait_for.interval = tm_val;
    } else if (insn->wait.func) {
        vm->waiting_until = 1;
        vm->wait_until.func = insn->wait.func;
        vm->wait_until.id = insn->wait.wlab_id;
    }

    qvm->parent = vm;
    vm = qvm;
    return EXEC_OK;
}

static int insn_wlab(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_WLAB);
    vm->last_passed.func = insn->wlab.func;
    vm->last_passed.id = insn->wlab.id;
    return EXEC_OK;
}

static int insn_getsp(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_GETSP);

    const uint8_t dst_is_temp = insn->dst.opd == CODEGEN_OPD_TEMP;
    const uint8_t dst_signd = insn->dst.signd;
    const uint8_t dst_indirect = insn->dst.indirect;
    const uint64_t dst_size = opd_size(&insn->dst);

    LEGAL_IF(dst_is_temp == 1, "%u", dst_is_temp);
    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(dst_indirect == 0, "%u", dst_indirect);
    LEGAL_IF(dst_size == 8, "%" PRIu64, dst_size);

    *((uint64_t *) opd_val(&insn->dst)) = vm->sp;
    return EXEC_OK;
}

static int insn_qnt(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_QNT);

    const uint8_t dst_signd = insn->qnt.dst.signd;
    const uint8_t dst_indirect = insn->qnt.dst.indirect;
    const uint64_t dst_size = opd_size(&insn->qnt.dst);

    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(dst_indirect == 0, "%u", dst_indirect);
    LEGAL_IF(dst_size == 8, "%" PRIu64, dst_size);

    const uint8_t loc_signd = insn->qnt.loc.signd;
    const uint8_t loc_indirect = insn->qnt.loc.indirect;
    const uint64_t loc_size = opd_size(&insn->qnt.loc);

    LEGAL_IF(loc_signd == 0, "%u", loc_signd);
    LEGAL_IF(loc_indirect == 0 || loc_indirect == 1, "%u", loc_indirect);
    LEGAL_IF(loc_size == 8, "%" PRIu64, loc_size);

    const uint8_t ssp_is_temp = insn->qnt.sp.opd == CODEGEN_OPD_TEMP;
    const uint8_t ssp_signd = insn->qnt.sp.signd;
    const uint8_t ssp_indirect = insn->qnt.sp.indirect;
    const uint64_t ssp_size = opd_size(&insn->qnt.sp);

    LEGAL_IF(ssp_is_temp == 1, "%u", ssp_is_temp);
    LEGAL_IF(ssp_signd == 0, "%u", ssp_signd);
    LEGAL_IF(ssp_indirect == 0, "%u", ssp_indirect);
    LEGAL_IF(ssp_size == 8, "%" PRIu64, ssp_size);

    uint64_t *const dst = opd_val(&insn->qnt.dst);
    const uint64_t loc = *(uint64_t *) opd_val(&insn->qnt.loc);
    const uint64_t ssp = *(uint64_t *) opd_val(&insn->qnt.sp);

    struct qvm *const qvm = calloc(1, sizeof(struct qvm) + STACK_SIZE);

    if (!qvm) {
        return EXEC_NOMEM;
    }

    qvm->ip = loc;
    qvm->bp = 8 * 2;
    qvm->sp = qvm->bp + vm->sp - ssp;
    qvm->at_start = 1;
    memcpy(qvm->stack + qvm->bp, vm->stack + ssp, (size_t) (vm->sp - ssp));
    *dst = (uint64_t) (uintptr_t) qvm;
    vm->sp = ssp;
    return EXEC_OK;
}

static int insn_qntv(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_QNTV);

    const uint8_t dst_signd = insn->qntv.dst.signd;
    const uint8_t dst_indirect = insn->qntv.dst.indirect;
    const uint64_t dst_size = opd_size(&insn->qntv.dst);

    LEGAL_IF(dst_signd == 0, "%u", dst_signd);
    LEGAL_IF(dst_indirect == 0, "%u", dst_indirect);
    LEGAL_IF(dst_size == 8, "%" PRIu64, dst_size);

    const uint8_t val_signd = insn->qntv.val.signd;
    const uint8_t val_indirect = insn->qntv.val.indirect;
    const uint64_t val_size = opd_size(&insn->qntv.val);

    LEGAL_IF(val_signd == 0 || val_signd == 1, "%u", val_signd);
    LEGAL_IF(val_indirect == 0 || val_indirect == 1, "%u", val_indirect);
    LEGAL_IF(val_size > 0, "%" PRIu64, val_size);

    uint64_t *const dst = opd_val(&insn->qntv.dst);
    const void *const val = opd_val(&insn->qntv.val);

    struct qvm *const qvm = calloc(1, sizeof(struct qvm) + STACK_SIZE);

    if (!qvm) {
        return EXEC_NOMEM;
    }

    qvm->at_start = 1;
    qvm->at_end = 1;
    memcpy(qvm->stack, val, (size_t) val_size);
    *dst = (uint64_t) (uintptr_t) qvm;
    return EXEC_OK;
}

static int insn_noint_int(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_NOINT || insn->op == CODEGEN_OP_INT);
    vm->noint = insn->op == CODEGEN_OP_NOINT;
    return EXEC_OK;
}

static int insn_bfun(const struct codegen_insn *const insn)
{
    assert(insn->op == CODEGEN_OP_BFUN);

    LEGAL_IF(vm->sp % 8 == 0, "%" PRIu64, vm->sp);
    LEGAL_IF(vm->bp % 8 == 0, "%" PRIu64, vm->bp);
    LEGAL_IF(vm->sp >= 16, "%" PRIu64, vm->sp);

    uint64_t retval_size = 0;
    const void *retval = NULL;

    switch (vm->ip) {
    case SCOPE_BFUN_ID_NULL:
        LEGAL_IF(0, "null function call");
        break;

    case SCOPE_BFUN_ID_MONOTIME:
        if (unlikely(get_monotonic_time(&now))) {
            return EXEC_NOMEM;
        }

        retval_size = sizeof(now);
        retval = &now;
        break;

    case SCOPE_BFUN_ID_MALLOC:
    case SCOPE_BFUN_ID_CALLOC: {
        LEGAL_IF(vm->sp >= 16 + 8, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 8 <= STACK_SIZE, "%" PRIu64, vm->bp);
        retval_size = 8;
        const size_t size = (size_t) *(uint64_t *) (vm->stack + vm->bp);

        const uint64_t mem = vm->ip == SCOPE_BFUN_ID_MALLOC ?
            (uint64_t) (uintptr_t) malloc(size) : (uint64_t) (uintptr_t) calloc(1, size);

        retval = &mem;
        vm->sp -= 8;
    } break;

    case SCOPE_BFUN_ID_REALLOC: {
        LEGAL_IF(vm->sp >= 16 + 16, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 16 <= STACK_SIZE, "%" PRIu64, vm->bp);
        retval_size = 8;
        void *const oldptr = (void *) (uintptr_t) *(uint64_t *) (vm->stack + vm->bp);
        const size_t newsize = (size_t) *(uint64_t *) (vm->stack + vm->bp + 8);
        const uint64_t mem = (uint64_t) (uintptr_t) realloc(oldptr, newsize);
        retval = &mem;
        vm->sp -= 16;
    } break;

    case SCOPE_BFUN_ID_FREE: {
        LEGAL_IF(vm->sp >= 16 + 8, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 8 <= STACK_SIZE, "%" PRIu64, vm->bp);
        void *const ptr = (void *) (uintptr_t) *(uint64_t *) (vm->stack + vm->bp);
        free(ptr);
        vm->sp -= 8;
    } break;

    case SCOPE_BFUN_ID_PS:
        LEGAL_IF(vm->sp >= 16 + 8, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 8 <= STACK_SIZE, "%" PRIu64, vm->bp);
        printf("%s", (const char *) (uintptr_t) *(uint64_t *) (vm->stack + vm->bp));
        fflush(stdout);
        vm->sp -= 8;
        break;

    case SCOPE_BFUN_ID_PU8:
    case SCOPE_BFUN_ID_PI8:
        LEGAL_IF(vm->sp >= 16 + 8, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 1 <= STACK_SIZE, "%" PRIu64, vm->bp);

        vm->ip == SCOPE_BFUN_ID_PU8 ?
            printf("%" PRIu8, *(uint8_t *) (vm->stack + vm->bp)) :
            printf("%" PRIi8, *(int8_t *) (vm->stack + vm->bp));

        fflush(stdout);
        vm->sp -= 8;
        break;

    case SCOPE_BFUN_ID_PU16:
    case SCOPE_BFUN_ID_PI16:
        LEGAL_IF(vm->sp >= 16 + 8, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 2 <= STACK_SIZE, "%" PRIu64, vm->bp);

        vm->ip == SCOPE_BFUN_ID_PU16 ?
            printf("%" PRIu16, *(uint16_t *) (vm->stack + vm->bp)) :
            printf("%" PRIi16, *(int16_t *) (vm->stack + vm->bp));

        fflush(stdout);
        vm->sp -= 8;
        break;

    case SCOPE_BFUN_ID_PU32:
    case SCOPE_BFUN_ID_PI32:
        LEGAL_IF(vm->sp >= 16 + 8, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 4 <= STACK_SIZE, "%" PRIu64, vm->bp);

        vm->ip == SCOPE_BFUN_ID_PU32 ?
            printf("%" PRIu32, *(uint32_t *) (vm->stack + vm->bp)) :
            printf("%" PRIi32, *(int32_t *) (vm->stack + vm->bp));

        fflush(stdout);
        vm->sp -= 8;
        break;

    case SCOPE_BFUN_ID_PU64:
    case SCOPE_BFUN_ID_PI64:
        LEGAL_IF(vm->sp >= 16 + 8, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 8 <= STACK_SIZE, "%" PRIu64, vm->bp);

        vm->ip == SCOPE_BFUN_ID_PU64 ?
            printf("%" PRIu64, *(uint64_t *) (vm->stack + vm->bp)) :
            printf("%" PRIi64, *(int64_t *) (vm->stack + vm->bp));

        fflush(stdout);
        vm->sp -= 8;
        break;

    case SCOPE_BFUN_ID_PNL:
        printf("\n");
        fflush(stdout);
        break;

    case SCOPE_BFUN_ID_EXIT:
        LEGAL_IF(vm->sp >= 16 + 8, "%" PRIu64, vm->sp);
        LEGAL_IF(vm->bp + 4 <= STACK_SIZE, "%" PRIu64, vm->bp);
        vm->sp -= 8;
        exit(*(int32_t *) (vm->stack + vm->bp));
        break;
    }

    vm->sp -= 16;
    struct tmp_frame *const new_tmp_frame = malloc(sizeof(struct tmp_frame));

    if (unlikely(!new_tmp_frame)) {
        return EXEC_NOMEM;
    }

    new_tmp_frame->prev = vm->temps;
    vm->temps = new_tmp_frame;

    return handle_return(insn, retval_size, retval);
}

static int exec_insn(const struct codegen_insn *const insn)
{
    int result = EXEC_OK;

    switch (insn->op) {
    case CODEGEN_OP_NOP:
        result = EXEC_OK;
        break;

    case CODEGEN_OP_MOV:
        result = insn_mov(insn);
        break;

    case CODEGEN_OP_CAST:
        result = insn_cast(insn);
        break;

    case CODEGEN_OP_ADD:
    case CODEGEN_OP_SUB:
    case CODEGEN_OP_MUL:
    case CODEGEN_OP_DIV:
    case CODEGEN_OP_MOD:
    case CODEGEN_OP_LSH:
    case CODEGEN_OP_RSH:
    case CODEGEN_OP_AND:
    case CODEGEN_OP_XOR:
    case CODEGEN_OP_OR:
        result = insn_bin_arith(insn);
        break;

    case CODEGEN_OP_EQU:
    case CODEGEN_OP_NEQ:
        result = insn_equ_neq(insn);
        break;

    case CODEGEN_OP_LT:
    case CODEGEN_OP_GT:
    case CODEGEN_OP_LTE:
    case CODEGEN_OP_GTE:
        result = insn_bin_logic(insn);
        break;

    case CODEGEN_OP_NOT:
        result = insn_not(insn);
        break;

    case CODEGEN_OP_NEG:
        result = insn_neg(insn);
        break;

    case CODEGEN_OP_BNEG:
        result = insn_bneg(insn);
        break;

    case CODEGEN_OP_OZ:
        result = insn_oz(insn);
        break;

    case CODEGEN_OP_INC:
    case CODEGEN_OP_DEC:
        result = insn_inc_dec(insn);
        break;

    case CODEGEN_OP_INCP:
    case CODEGEN_OP_DECP:
        result = insn_incp_decp(insn);
        break;

    case CODEGEN_OP_JZ:
    case CODEGEN_OP_JNZ:
        result = insn_cjmp(insn);
        break;

    case CODEGEN_OP_JMP:
        result = insn_jmp(insn);
        break;

    case CODEGEN_OP_PUSHR:
        result = insn_pushr(insn);
        break;

    case CODEGEN_OP_PUSH:
        result = insn_push(insn);
        break;

    case CODEGEN_OP_CALL:
    case CODEGEN_OP_CALLV:
        result = insn_call_callv(insn);
        break;

    case CODEGEN_OP_INCSP:
        result = insn_incsp(insn);
        break;

    case CODEGEN_OP_RET:
    case CODEGEN_OP_RETV:
        result = insn_ret_retv(insn);
        break;

    case CODEGEN_OP_REF:
        result = insn_ref(insn);
        break;

    case CODEGEN_OP_DRF:
        result = insn_drf(insn);
        break;

    case CODEGEN_OP_RTE:
    case CODEGEN_OP_RTEV:
        result = insn_rte_rtev(insn);
        break;

    case CODEGEN_OP_QAT:
        result = insn_qat(insn);
        break;

    case CODEGEN_OP_WAIT:
        result = insn_wait(insn);
        break;

    case CODEGEN_OP_WLAB:
        result = insn_wlab(insn);
        break;

    case CODEGEN_OP_GETSP:
        result = insn_getsp(insn);
        break;

    case CODEGEN_OP_QNT:
        result = insn_qnt(insn);
        break;

    case CODEGEN_OP_QNTV:
        result = insn_qntv(insn);
        break;

    case CODEGEN_OP_NOINT:
    case CODEGEN_OP_INT:
        result = insn_noint_int(insn);
        break;

    case CODEGEN_OP_BFUN:
        result = insn_bfun(insn);
        break;

    default: LEGAL_IF(false, "unknown instruction: %u", insn->op);
    }

    switch (insn->op) {
    case CODEGEN_OP_JZ:
    case CODEGEN_OP_JNZ:
    case CODEGEN_OP_JMP:
    case CODEGEN_OP_CALL:
    case CODEGEN_OP_CALLV:
    case CODEGEN_OP_RET:
    case CODEGEN_OP_RETV:
    case CODEGEN_OP_BFUN:
    case CODEGEN_OP_RTE:
    case CODEGEN_OP_RTEV:
    case CODEGEN_OP_WAIT:
        break;

    default:
        vm->ip++;
    }

    check_and_eventually_split_vms();
    return result;
}

int exec(const struct codegen_obj *const obj)
{
    if (!(bss = calloc(1, obj->data_size + obj->strings.size))) {
        return EXEC_NOMEM;
    }

    if (!(vm = calloc(1, sizeof(struct qvm) + STACK_SIZE))) {
        free(bss);
        return EXEC_NOMEM;
    }

    if (obj->strings.mem) {
        memcpy(bss + obj->data_size, obj->strings.mem, obj->strings.size);
    }

    bss_size = obj->data_size + obj->strings.size;
    *(uint64_t *) vm->stack = obj->insn_count;
    vm->sp = vm->bp = 16;
    vm->ip = SCOPE_BFUN_ID_COUNT;

    o = obj;
    int error;

    do {
        if ((error = exec_insn(&o->insns[vm->ip]))) {
            break;
        }

        LEGAL_IF(vm->ip <= o->insn_count, "%" PRIu64, vm->ip);
    } while (vm->ip < o->insn_count);

    free(vm);
    free(bss);
    return error;
}
