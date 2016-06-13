#include "type.h"

#include "lex.h"
#include "ast.h"
#include "scope.h"

#include "common.h"

#include <stdlib.h>
#include <inttypes.h>
#include <memory.h>
#include <assert.h>

#define NOMEM \
    (fprintf(stderr, "%s:%d: no memory\n", __FILE__, __LINE__), TYPE_NOMEM)

#define NOMEM_NULL \
    ((void) NOMEM, NULL)

#define INVALID_NULL(d, n) ({ \
    lex_print_error(stderr, (d), (n)->ltok, (n)->rtok); \
    NULL; \
})

#define INVALID(d, n) ({ \
    lex_print_error(stderr, (d), (n)->ltok, (n)->rtok); \
    TYPE_INVALID; \
})

#define td(n, a, s) { \
    .name  = { n },         \
    .alias = { a },         \
    .nlen  = sizeof(n) - 1, \
    .alen  = sizeof(a) - 1, \
    .size  = s,             \
},

static const struct typedesc {
    const char name[8], alias[8];
    const uint8_t nlen: 3, alen: 3, size: 4;
} typedescs[TYPE_COUNT] = {
    td("byte"   , "u8"     , 1)
    td("sbyte"  , "i8"     , 1)
    td("ushort" , "u16"    , 2)
    td("short"  , "i16"    , 2)
    td("uint"   , "u32"    , 4)
    td("int"    , "i32"    , 4)
    td("ulong"  , "u64"    , 8)
    td("long"   , "i64"    , 8)
    td("usize"  , ""       , 8)
    td("ssize"  , ""       , 8)
    td("uptr"   , ""       , 8)
    td("iptr"   , ""       , 8)
    td("enum"   , ""       , 0)
    td("ptr"    , ""       , 8)
    td("vptr"   , ""       , 8)
    td("fptr"   , ""       , 8)
    td("quaint" , ""       , 8)
    td("struct" , ""       , 0)
    td("union"  , ""       , 0)
};

#undef td

static struct {
    size_t size, allocated;

    struct {
        const struct lex_symbol *name;
        const struct type *type;
    } *entries;
} symtab;

static struct type
    *const u8    = type(U8,  1),
    *const u16   = type(U16, 1),
    *const u32   = type(U32, 1),
    *const u64   = type(U64, 1),
    *const u8ptr = type_ptr(1, type(U8, 1));

static const struct scope *unit_scope;

type_t type_match(const struct lex_symbol *const symbol)
{
    const size_t len = (size_t) (symbol->end - symbol->beg);

    for (type_t type = TYPE_VOID + 1; type < TYPE_COUNT; ++type) {
        const struct typedesc *const td = &typedescs[type - 1];

        if (len == td->nlen && !memcmp(td->name, symbol->beg, len)) {
            return type;
        }

        if (len == td->alen && !memcmp(td->alias, symbol->beg, len)) {
            return type;
        }
    }

    return TYPE_VOID;
}

const struct type *type_symtab_find(const struct lex_symbol *const symbol)
{
    for (size_t entry_idx = 0; entry_idx < symtab.size; ++entry_idx) {
        if (lex_symbols_equal(symtab.entries[entry_idx].name, symbol)) {
            return symtab.entries[entry_idx].type;
        }
    }

    return NULL;
}

int type_symtab_insert(const struct lex_symbol *const symbol,
    const struct type *const type)
{
    if (unlikely(type_symtab_find(symbol))) {
        return 1;
    }

    if (symtab.size >= symtab.allocated) {
        symtab.allocated = (symtab.allocated ?: 1) * 8;

        void *const tmp = realloc(symtab.entries,
            symtab.allocated * sizeof(symtab.entries[0]));

        if (unlikely(!tmp)) {
            return type_symtab_clear(), -1;
        }

        symtab.entries = tmp;
    }

    symtab.entries[symtab.size].name = symbol;
    symtab.entries[symtab.size++].type = type;
    return 0;
}

void type_symtab_clear(void)
{
    for (size_t entry_idx = 0; entry_idx < symtab.size; ++entry_idx) {
        type_free(symtab.entries[entry_idx].type);
    }

    free(symtab.entries);
    symtab.entries = NULL;
    symtab.size = 0;
    symtab.allocated = 0;
}

void type_print(FILE *const out, const struct type *type)
{
    static const char *const types[] = {
        "void",
        "u8",
        "i8",
        "u16",
        "i16",
        "u32",
        "i32",
        "u64",
        "i64",
        "usize",
        "ssize",
        "uptr",
        "iptr",
        "enum",
        "ptr",
        "vptr",
        "fptr",
        "quaint",
        "struct",
        "union",
        "COUNT",
    };

    static_assert(countof(types) == TYPE_COUNT + 1, "type mismatch");

    switch (type->t) {
    case TYPE_PTR:
    case TYPE_QUAINT:
        if (type->count == 1) {
            fprintf(out, YELLOW("%s"), types[type->t]);
        } else {
            fprintf(out, YELLOW("%s[%zu]"), types[type->t], type->count);
        }

        fprintf(out, GREEN(" ➔ "));
        type_print(out, type->subtype);
        break;

    case TYPE_STRUCT:
    case TYPE_UNION:
        if (type->count == 1) {
            fprintf(out, YELLOW("%s"), types[type->t]);
        } else {
            fprintf(out, YELLOW("%s[%zu]"), types[type->t], type->count);
        }

        fprintf(out, GREEN("("));

        for (size_t idx = 0; idx < type->member_count; ++idx) {
            lex_print_symbol(out, CYAN("%.*s"), type->members[idx].name);
            fprintf(out, GREEN(": "));
            type_print(out, type->members[idx].type);

            if (idx != type->member_count - 1) {
                fprintf(out, GREEN(", "));
            }
        }

        fprintf(out, GREEN(")"));
        break;

    case TYPE_FPTR:
        if (type->count == 1) {
            fprintf(out, YELLOW("%s"), types[type->t]);
        } else {
            fprintf(out, YELLOW("%s[%zu]"), types[type->t], type->count);
        }

        fprintf(out, GREEN("("));

        for (size_t idx = 0; idx < type->param_count; ++idx) {
            lex_print_symbol(out, CYAN("%.*s"), type->params[idx].name);
            fprintf(out, GREEN(": "));
            type_print(out, type->params[idx].type);

            if (idx != type->param_count - 1) {
                fprintf(out, GREEN(", "));
            }
        }

        fprintf(out, GREEN(")"));

        if (type->rettype) {
            fprintf(out, GREEN(": "));
            type_print(out, type->rettype);
        }
        break;

    case TYPE_ENUM:
        if (type->count == 1) {
            fprintf(out, YELLOW("%s"), types[type->t]);
        } else {
            fprintf(out, YELLOW("%s[%zu]"), types[type->t], type->count);
        }

        fprintf(out, GREEN("("));

        for (size_t idx = 0; idx < type->value_count; ++idx) {
            lex_print_symbol(out, CYAN("%.*s"), type->values[idx].name);
            fprintf(out, GREEN(" = ") CYAN("%" PRIu64), type->values[idx].value);

            if (idx != type->value_count - 1) {
                fprintf(out, GREEN(", "));
            }
        }

        fprintf(out, GREEN("): ") YELLOW("%s"), types[type->t_value]);
        break;

    default:
        if (type->count == 1) {
            fprintf(out, YELLOW("%s"), types[type->t]);
        } else {
            fprintf(out, YELLOW("%s[%zu]"), types[type->t], type->count);
        }
    }
}

void type_free(const struct type *const type)
{
    if (!type) {
        return;
    }

    switch (type->t) {
    case TYPE_PTR:
    case TYPE_QUAINT:
        type_free(type->subtype);
        break;

    case TYPE_STRUCT:
    case TYPE_UNION:
        for (size_t idx = 0; idx < type->member_count; ++idx) {
            type_free(type->members[idx].type);
        }

        free(type->members);
        free(type->offsets);
        break;

    case TYPE_FPTR:
        for (size_t idx = 0; idx < type->param_count; ++idx) {
            type_free(type->params[idx].type);
        }

        free(type->params);
        type_free(type->rettype);
        break;

    case TYPE_ENUM:
        free(type->values);
        break;
    }

    free((struct type *) type);
}

int type_copy(struct type *const dst, const struct type *const src)
{
    dst->t = src->t;
    dst->count = src->count;

    switch (src->t) {
    case TYPE_PTR:
    case TYPE_QUAINT:
        if (unlikely(!(dst->subtype = type_alloc))) {
            return TYPE_NOMEM;
        }

        return type_copy(dst->subtype, src->subtype);

    case TYPE_STRUCT:
    case TYPE_UNION:
        if (unlikely(!(dst->members = calloc(src->member_count,
            sizeof(struct type_nt_pair))))) {

            return TYPE_NOMEM;
        }

        dst->member_count = src->member_count;

        for (size_t idx = 0; idx < src->member_count; ++idx) {
            dst->members[idx].name = src->members[idx].name;

            if (unlikely(!(dst->members[idx].type = type_alloc))) {
                return TYPE_NOMEM;
            }

            if (unlikely(type_copy(dst->members[idx].type,
                src->members[idx].type))) {

                return TYPE_NOMEM;
            }
        }

        break;

    case TYPE_FPTR:
        if (src->rettype) {
            if (unlikely(!(dst->rettype = type_alloc))) {
                return TYPE_NOMEM;
            }

            if (unlikely(type_copy(dst->rettype, src->rettype))) {
                return TYPE_NOMEM;
            }
        }

        if (!src->param_count) {
            return TYPE_OK;
        }

        if (unlikely(!(dst->params = calloc(src->param_count,
            sizeof(struct type_nt_pair))))) {

            return TYPE_NOMEM;
        }

        dst->param_count = src->param_count;

        for (size_t idx = 0; idx < src->param_count; ++idx) {
            dst->params[idx].name = src->params[idx].name;

            if (unlikely(!(dst->params[idx].type = type_alloc))) {
                return TYPE_NOMEM;
            }

            if (unlikely(type_copy(dst->params[idx].type,
                src->params[idx].type))) {

                return TYPE_NOMEM;
            }
        }

        break;

    case TYPE_ENUM:
        if (unlikely(!(dst->values = calloc(src->value_count,
            sizeof(struct type_nv_pair))))) {

            return TYPE_NOMEM;
        }

        dst->value_count = src->value_count;
        dst->t_value = src->t_value;

        for (size_t idx = 0; idx < src->value_count; ++idx) {
            dst->values[idx].name = src->values[idx].name;
            dst->values[idx].value = src->values[idx].value;
        }

        break;
    }

    return TYPE_OK;
}

bool type_equals(const struct type *const ta, const struct type *const tb)
{
    assert(ta != NULL);
    assert(tb != NULL);

    if (ta->t != tb->t || ta->count != tb->count) {
        return false;
    }

    switch (ta->t) {
    case TYPE_PTR:
    case TYPE_QUAINT:
        return type_equals(ta->subtype, tb->subtype);

    case TYPE_STRUCT:
    case TYPE_UNION: {
        if (ta->member_count != tb->member_count) {
            return false;
        }

        for (size_t idx = 0; idx < ta->member_count; ++idx) {
            if (!type_equals(ta->members[idx].type, tb->members[idx].type)) {
                return false;
            }
        }

        return true;
    }

    case TYPE_FPTR: {
        if (ta->param_count != tb->param_count) {
            return false;
        }

        for (size_t idx = 0; idx < ta->param_count; ++idx) {
            if (!type_equals(ta->params[idx].type, tb->params[idx].type)) {
                return false;
            }
        }

        if (!ta->rettype && !tb->rettype) {
            return true;
        } else if (ta->rettype && tb->rettype) {
            return type_equals(ta->rettype, tb->rettype);
        } else {
            return false;
        }
    }

    case TYPE_ENUM: {
        if (ta->value_count != tb->value_count || ta->t_value != tb->t_value) {
            return false;
        }

        for (size_t idx = 0; idx < ta->value_count; ++idx) {
            if (!lex_symbols_equal(ta->values[idx].name, tb->values[idx].name)) {
                return false;
            }

            if (ta->values[idx].value != tb->values[idx].value) {
                return false;
            }
        }

        return true;
    }

    default: return true;
    }
}

int type_quantify(struct type *const type)
{
    assert(type != NULL);
    assert(type->size == 0);
    assert(type->alignment == 0);

    if (type->t == TYPE_VOID) {
        return TYPE_OK;
    }

    const size_t size =
        typedescs[(type->t != TYPE_ENUM ? type->t : type->t_value) - 1].size;

    if (size) {
        type->size = size;
        type->alignment = size;
        return TYPE_OK;
    }

    assert(type->member_count != 0);
    assert(type->offsets == NULL);

    if (unlikely(!(type->offsets = calloc(type->member_count, sizeof(size_t))))) {
        return NOMEM;
    }

    switch (type->t) {
    case TYPE_STRUCT: {
        size_t member_idx = 0, current_offset = 0, greatest_alignment = 0;

        do {
            struct type *const member_type = type->members[member_idx].type;

            if (unlikely(type_quantify(member_type))) {
                return TYPE_NOMEM;
            }

            const size_t member_size = member_type->size * member_type->count;
            const size_t member_alignment = member_type->alignment;

            if (member_alignment > greatest_alignment) {
                greatest_alignment = member_alignment;
            }

            ALIGN_UP(current_offset, member_alignment);
            type->offsets[member_idx] = current_offset;
            current_offset += member_size;
        } while (++member_idx < type->member_count);

        ALIGN_UP(current_offset, greatest_alignment);
        type->size = current_offset;
        type->alignment = greatest_alignment;
    } break;

    case TYPE_UNION: {
        size_t member_idx = 0, greatest_size = 0, greatest_alignment = 0;

        do {
            struct type *const member_type = type->members[member_idx].type;

            if (unlikely(type_quantify(member_type))) {
                return TYPE_NOMEM;
            }

            const size_t member_size = member_type->size * member_type->count;
            const size_t member_alignment = member_type->alignment;

            if (member_size > greatest_size) {
                greatest_size = member_size;
            }

            if (member_alignment > greatest_alignment) {
                greatest_alignment = member_alignment;
            }
        } while (++member_idx < type->member_count);

        type->size = greatest_size;
        type->alignment = greatest_alignment;
    } break;

    default: assert(0), abort();
    }

    return TYPE_OK;
}

const struct type *type_of_expr(const struct ast_node *const expr)
{
    assert(expr != NULL);

    switch (expr->an) {
    case AST_AN_BEXP: return ast_data(expr, bexp)->type;
    case AST_AN_UEXP: return ast_data(expr, uexp)->type;
    case AST_AN_FEXP: return ast_data(expr, fexp)->type;
    case AST_AN_XEXP: return ast_data(expr, xexp)->type;
    case AST_AN_AEXP: return ast_data(expr, aexp)->type;
    case AST_AN_TEXP: return ast_data(expr, texp)->type;
    case AST_AN_NMBR: return ast_data(expr, nmbr)->type;
    case AST_AN_STRL: return ast_data(expr, strl)->type;
    case AST_AN_NAME: return ast_data(expr, name)->type;
    default: assert(0), abort();
    }
}

static bool expr_is_lvalue(const struct ast_node *const node)
{
    assert(node != NULL);

    switch (node->an) {
    case AST_AN_VOID: assert(0), abort();

    case AST_AN_BEXP: {
        const struct ast_bexp *const bexp = ast_data(node, bexp);
        assert(bexp->type != NULL);

        if (bexp->op == LEX_TK_MEMB) {
            return expr_is_lvalue(bexp->lhs);
        } else if (bexp->op == LEX_TK_AROW) {
            return true;
        }
    } break;

    case AST_AN_UEXP: {
        const struct ast_uexp *const uexp = ast_data(node, uexp);
        assert(uexp->type != NULL);

        if (uexp->op == LEX_TK_MULT && type_of_expr(uexp->rhs)->t != TYPE_QUAINT) {
            return true;
        }
    } break;

    case AST_AN_FEXP: break;
    case AST_AN_XEXP: break;

    case AST_AN_AEXP: {
        const struct ast_aexp *const aexp = ast_data(node, aexp);
        assert(aexp->type != NULL);
        return expr_is_lvalue(aexp->base);
    }

    case AST_AN_TEXP: break;

    case AST_AN_NAME: {
        const struct ast_name *const name = ast_data(node, name);
        assert(name->type != NULL);
        assert(name->scoped != NULL);

        switch (name->scoped->obj) {
        case SCOPE_OBJ_BCON:
            return INVALID("builtin constant is not modifiable", node), false;

        case SCOPE_OBJ_BFUN:
            return INVALID("builtin func is not modifiable", node), false;

        case SCOPE_OBJ_GVAR:
        case SCOPE_OBJ_AVAR: {
            assert(name->scoped->decl != NULL);

            if (ast_data(name->scoped->decl, decl)->cons) {
                return INVALID("constant is not modifiable", node), false;
            }
        } break;

        case SCOPE_OBJ_FUNC:
            return INVALID("function is not modifiable", node), false;

        case SCOPE_OBJ_PARM: break;

        default: assert(0), abort();
        }

        return true;
    }

    case AST_AN_NMBR: break;
    case AST_AN_STRL: break;

    default: assert(0), abort();
    }

    return INVALID("lvalue is required", node), false;
}

static const struct type *type_from_name(const struct ast_node *const node,
    const struct scope *const scope)
{
    struct ast_name *const name = ast_data(node, name);
    assert(name->type == NULL);

    const struct scope_obj *const found =
        scope_find_object(scope, (const struct lex_symbol *) node->ltok);

    if (!found) {
        return INVALID_NULL("undefined symbol", node);
    }

    name->scoped = found;

    if (unlikely(!(name->type = type_alloc))) {
        return NOMEM_NULL;
    }

    switch (found->obj) {
    case SCOPE_OBJ_BCON:
    case SCOPE_OBJ_PARM:
    case SCOPE_OBJ_GVAR:
    case SCOPE_OBJ_AVAR: {
        const struct type *src_type;

        if (found->obj == SCOPE_OBJ_GVAR || found->obj == SCOPE_OBJ_AVAR) {
            src_type = ast_data(found->decl, decl)->type;
        } else if (found->obj == SCOPE_OBJ_PARM) {
            src_type = found->type;
        } else {
            src_type = scope_builtin_consts[found->bcon_id].type;
        }

        if (unlikely(type_copy(name->type, src_type))) {
            return type_free(name->type), name->type = NULL, NOMEM_NULL;
        }
    } break;

    case SCOPE_OBJ_BFUN:
    case SCOPE_OBJ_FUNC: {
        struct type src_type = (struct type) {
            .t = TYPE_FPTR,
            .count = 1,
        };

        if (found->obj == SCOPE_OBJ_BFUN) {
            const struct scope_builtin_func *const bfun =
                &scope_builtin_funcs[found->bfun_id];

            src_type.param_count = bfun->param_count;
            src_type.params = (struct type_nt_pair *) bfun->params;
            src_type.rettype = (struct type *) bfun->rettype;
        } else {
            const struct ast_func *const func = ast_data(found->func, func);
            src_type.param_count = func->param_count;
            src_type.params = func->params;
            src_type.rettype = func->rettype;
        }

        if (unlikely(type_copy(name->type, &src_type))) {
            return type_free(name->type), name->type = NULL, NOMEM_NULL;
        }
    } break;

    default: assert(0), abort();
    }

    return name->type;
}

static const struct type *type_from_scoped_name(const struct ast_node *const node,
    const struct scope *const scope)
{
    struct ast_bexp *const bexp = ast_data(node, bexp);
    (void) bexp;
    (void) scope;
    assert(bexp->op == LEX_TK_SCOP);

    const struct ast_node *list = node;

    do {
        const bool not_last =
            list->an == AST_AN_BEXP && ast_data(list, bexp)->op == LEX_TK_SCOP;

        const struct ast_node *const name = not_last ? ast_data(list, bexp)->lhs : list;
        list = not_last ? ast_data(list, bexp)->rhs : NULL;

        if (name->an != AST_AN_NAME) {
            return INVALID_NULL("only a name can appear in a scope operator", name);
        }

        //lex_print_symbol(stdout, "%.*s\n", (const struct lex_symbol *) name->ltok);
    } while (list);

    return INVALID_NULL("operator not implemented", node);
}

static const struct type *type_from_expr(const struct ast_node *,
    const struct scope *);

static const struct type *type_from_bexp(const struct ast_node *const node,
    const struct scope *const scope)
{
    struct ast_bexp *const bexp = ast_data(node, bexp);
    assert(bexp->type == NULL);

    if (bexp->op == LEX_TK_SCOP) {
        return type_from_scoped_name(node, scope);
    }

    const struct type *const lhs_type = type_from_expr(bexp->lhs, scope);
    const struct type *rhs_type = NULL;

    const bool go_right =
        bexp->op != LEX_TK_CAST && bexp->op != LEX_TK_COLN &&
        bexp->op != LEX_TK_MEMB && bexp->op != LEX_TK_AROW &&
        bexp->op != LEX_TK_ATSI;

    if (go_right && !(rhs_type = type_from_expr(bexp->rhs, scope))) {
        return NULL;
    }

    if (!lhs_type) {
        return NULL;
    }

    const type_t tl = lhs_type->t, tr = (go_right ? rhs_type->t : 0);

    #define finalise (type_free(bexp->type), bexp->type = NULL)

    switch (bexp->op) {
    case LEX_TK_ASSN: {
        if (!expr_is_lvalue(bexp->lhs)) {
            return NULL;
        }

        if (!type_equals(lhs_type, rhs_type)) {
            return INVALID_NULL("incompatible types in assignment", node);
        }

        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        if (unlikely(type_copy(bexp->type, rhs_type))) {
            return finalise, NOMEM_NULL;
        }
    } break;

    case LEX_TK_ASPL:
    case LEX_TK_ASMI:
    case LEX_TK_ASMU:
    case LEX_TK_ASDI:
    case LEX_TK_ASMO: {
        if (lhs_type->count != 1 || rhs_type->count != 1) {
            return INVALID_NULL("operator requires scalar operands", node);
        }

        if (!expr_is_lvalue(bexp->lhs)) {
            return NULL;
        }

        if (bexp->op == LEX_TK_ASPL || bexp->op == LEX_TK_ASMI) {
            if (!type_is_integral(tl) && !type_is_ptr(tl)) {
                return INVALID_NULL("non-integral left operand", node);
            }

            if (tl == TYPE_VPTR) {
                return INVALID_NULL("arithmetic on void pointer", node);
            }

            if (tl == TYPE_FPTR) {
                return INVALID_NULL("arithmetic on function pointer", node);
            }
        } else if (!type_is_integral(tl)) {
            return INVALID_NULL("non-integral left operand", node);
        }

        if (!type_is_integral(tr)) {
            return INVALID_NULL("non-integral right operand", node);
        }

        if (lhs_type->size != rhs_type->size) {
            return INVALID_NULL("differing type sizes", node);
        }

        const int signd_l = type_is_integral(tl) ? type_is_signed(tl) : 0;
        const int signd_r = type_is_signed(tr);

        if (signd_l != signd_r) {
            return INVALID_NULL("operands differ in signedness", node);
        }

        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        if (unlikely(type_copy(bexp->type, lhs_type))) {
            return finalise, NOMEM_NULL;
        }
    } break;

    case LEX_TK_ASLS:
    case LEX_TK_ASRS:
    case LEX_TK_ASAN:
    case LEX_TK_ASXO:
    case LEX_TK_ASOR: {
        if (lhs_type->count != 1 || rhs_type->count != 1) {
            return INVALID_NULL("operator requires scalar operands", node);
        }

        if (!expr_is_lvalue(bexp->lhs)) {
            return NULL;
        }

        if (!type_is_integral(tl)) {
            return INVALID_NULL("non-integral left operand", node);
        }

        if (!type_is_integral(tr)) {
            return INVALID_NULL("non-integral right operand", node);
        }

        if (!type_is_unsigned(tl)) {
            return INVALID_NULL("signed left operand", node);
        }

        if (!type_is_unsigned(tr)) {
            return INVALID_NULL("signed right operand", node);
        }

        if (lhs_type->size != rhs_type->size) {
            return INVALID_NULL("differing type sizes", node);
        }

        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        if (unlikely(type_copy(bexp->type, lhs_type))) {
            return finalise, NOMEM_NULL;
        }
    } break;

    case LEX_TK_SCOP:
        assert(0), abort();

    case LEX_TK_ATSI: {
        if (lhs_type->count != 1) {
            return INVALID_NULL("@ requires scalar value", bexp->lhs);
        }

        if (!type_is_quaint(tl)) {
            return INVALID_NULL("@ requires an lhs quaint", bexp->lhs);
        }

        if (bexp->rhs->an != AST_AN_BEXP && bexp->rhs->an != AST_AN_NAME) {
            return INVALID_NULL("@ rhs must be a bexp or \"start\"", bexp->rhs);
        }

        if (bexp->rhs->an == AST_AN_NAME) {
            const struct lex_symbol *const name =
                (const struct lex_symbol *) bexp->rhs->ltok;

            if (lex_symbols_equal(lex_sym("start"), name)) {
                goto create_type;
            } else if (lex_symbols_equal(lex_sym("end"), name)) {
                bexp->func = (const struct ast_func *) (uintptr_t) 1;
                goto create_type;
            }

            return INVALID_NULL("@ rhs can only be \"start\" or \"end\"", bexp->rhs);
        }

        const struct ast_bexp *const rhs_bexp = ast_data(bexp->rhs, bexp);

        if (rhs_bexp->op != LEX_TK_SCOP) {
            return INVALID_NULL("invalid label", bexp->rhs);
        }

        const struct ast_node *const lhs = rhs_bexp->lhs;
        const struct ast_node *const rhs = rhs_bexp->rhs;

        if (lhs->an != AST_AN_NAME || rhs->an != AST_AN_NAME) {
            return INVALID_NULL("invalid label", bexp->rhs);
        }

        struct scope_obj *found = scope_find_object(unit_scope,
            (const struct lex_symbol *) lhs->ltok);

        if (!found || found->obj != SCOPE_OBJ_FUNC) {
            return INVALID_NULL("no such function", lhs);
        }

        const struct ast_func *const func = ast_data(found->func, func);
        const ptrdiff_t index = scope_find_wlab(func,
            (const struct lex_symbol *) rhs->ltok);

        if (index == -1) {
            return INVALID_NULL("no such label in function", bexp->rhs);
        }

        bexp->func = func;
        bexp->wlab_idx = (size_t) index;

create_type:
        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        bexp->type->t = TYPE_U8;
        bexp->type->count = 1;
    } break;

    case LEX_TK_MEMB:
    case LEX_TK_AROW: {
        if (lhs_type->count != 1) {
            return INVALID_NULL("operator requires scalar value", bexp->lhs);
        }

        if (bexp->rhs->an != AST_AN_NAME) {
            return INVALID_NULL("expecting a field name", bexp->rhs);
        }

        const struct lex_symbol *const field = (struct lex_symbol *) bexp->rhs->ltok;
        const struct type *member_type = NULL;

        if (bexp->op == LEX_TK_MEMB) {
            if (tl == TYPE_STRUCT || tl == TYPE_UNION) {
                for (size_t idx = 0; idx < lhs_type->member_count; ++idx) {
                    if (lex_symbols_equal(field, lhs_type->members[idx].name)) {
                        member_type = lhs_type->members[idx].type;
                        bexp->member_idx = idx;
                        break;
                    }
                }
            } else {
                return INVALID_NULL("expecting a union or a struct", bexp->lhs);
            }
        } else if (tl == TYPE_PTR && (lhs_type->subtype->t == TYPE_UNION ||
            lhs_type->subtype->t == TYPE_STRUCT)) {

            if (lhs_type->subtype->count != 1) {
                return INVALID_NULL("arrow requires scalar value", bexp->lhs);
            }

            for (size_t idx = 0; idx < lhs_type->subtype->member_count; ++idx) {
                if (lex_symbols_equal(field, lhs_type->subtype->members[idx].name)) {
                    member_type = lhs_type->subtype->members[idx].type;
                    bexp->member_idx = idx;
                    break;
                }
            }
        } else {
            return INVALID_NULL("expecting a pointer to union or struct", bexp->lhs);
        }

        if (!member_type) {
            return INVALID_NULL("member not found", bexp->rhs);
        }

        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        if (unlikely(type_copy(bexp->type, member_type))) {
            return finalise, NOMEM_NULL;
        }
    } break;

    case LEX_TK_EQUL:
    case LEX_TK_NEQL:
    case LEX_TK_LTHN:
    case LEX_TK_GTHN:
    case LEX_TK_LTEQ:
    case LEX_TK_GTEQ:
    case LEX_TK_CONJ:
    case LEX_TK_DISJ: {
        if (bexp->op == LEX_TK_EQUL || bexp->op == LEX_TK_NEQL) {
            if (lhs_type->count != rhs_type->count) {
                return INVALID_NULL("differing array sizes", node);
            }
        } else if (lhs_type->count != 1 || rhs_type->count != 1) {
            return INVALID_NULL("operator requires scalar operands", node);
        }

        if (!type_is_integral(tl) && !type_is_ptr(tl)) {
            return INVALID_NULL("non-integral left operand", node);
        }

        if (!type_is_integral(tr) && !type_is_ptr(tr)) {
            return INVALID_NULL("non-integral right operand", node);
        }

        if (lhs_type->size != rhs_type->size) {
            return INVALID_NULL("differing type sizes", node);
        }

        const int signd_l = type_is_integral(tl) ? type_is_signed(tl) : 0;
        const int signd_r = type_is_integral(tr) ? type_is_signed(tr) : 0;

        if (signd_l != signd_r) {
            return INVALID_NULL("operands differ in signedness", node);
        }

        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        bexp->type->t = TYPE_U8;
        bexp->type->count = 1;
    } break;

    case LEX_TK_PLUS:
    case LEX_TK_MINS:
    case LEX_TK_MULT:
    case LEX_TK_DIVI:
    case LEX_TK_MODU: {
        if (lhs_type->count != 1 || rhs_type->count != 1) {
            return INVALID_NULL("operator requires scalar operands", node);
        }

        if (bexp->op == LEX_TK_PLUS || bexp->op == LEX_TK_MINS) {
            if (!type_is_integral(tl) && !type_is_ptr(tl)) {
                return INVALID_NULL("non-integral left operand", node);
            }

            if (tl == TYPE_VPTR) {
                return INVALID_NULL("arithmetic on void pointer", node);
            }

            if (tl == TYPE_FPTR) {
                return INVALID_NULL("arithmetic on function pointer", node);
            }
        } else if (!type_is_integral(tl)) {
            return INVALID_NULL("non-integral left operand", node);
        }

        if (!type_is_integral(tr)) {
            return INVALID_NULL("non-integral right operand", node);
        }

        if (lhs_type->size != rhs_type->size) {
            return INVALID_NULL("differing type sizes", node);
        }

        const int signd_l = type_is_integral(tl) ? type_is_signed(tl) : 0;
        const int signd_r = type_is_signed(tr);

        if (signd_l != signd_r) {
            return INVALID_NULL("operands differ in signedness", node);
        }

        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        if (unlikely(type_copy(bexp->type, lhs_type))) {
            return finalise, NOMEM_NULL;
        }
    } break;

    case LEX_TK_LSHF:
    case LEX_TK_RSHF:
    case LEX_TK_AMPS:
    case LEX_TK_CARE:
    case LEX_TK_PIPE: {
        if (lhs_type->count != 1 || rhs_type->count != 1) {
            return INVALID_NULL("operator requires scalar operands", node);
        }

        if (!type_is_integral(tl)) {
            return INVALID_NULL("non-integral left operand", node);
        }

        if (!type_is_integral(tr)) {
            return INVALID_NULL("non-integral right operand", node);
        }

        if (!type_is_unsigned(tl)) {
            return INVALID_NULL("signed left operand", node);
        }

        if (!type_is_unsigned(tr)) {
            return INVALID_NULL("signed right operand", node);
        }

        if (lhs_type->size != rhs_type->size) {
            return INVALID_NULL("differing type sizes", node);
        }

        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        if (unlikely(type_copy(bexp->type, lhs_type))) {
            return finalise, NOMEM_NULL;
        }
    } break;

    case LEX_TK_COMA: {
        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        if (unlikely(type_copy(bexp->type, rhs_type))) {
            return finalise, NOMEM_NULL;
        }
    } break;

    case LEX_TK_CAST:
    case LEX_TK_COLN: {
        if (!bexp->cast) {
            return INVALID_NULL("invalid type in cast", node);
        }

        if (tl == TYPE_VOID) {
            return INVALID_NULL("type not convertible", node);
        }

        if (unlikely(!(bexp->type = type_alloc))) {
            return NOMEM_NULL;
        }

        if (unlikely(type_copy(bexp->type, bexp->cast))) {
            return finalise, NOMEM_NULL;
        }
    } break;

    default: assert(0), abort();
    }

    #undef finalise
    return bexp->type;
}

static const struct type *type_from_uexp(const struct ast_node *const node,
    const struct scope *const scope)
{
    struct ast_uexp *const uexp = ast_data(node, uexp);
    assert(uexp->type == NULL);

    if (unlikely(!(uexp->type = type_alloc))) {
        return NOMEM_NULL;
    }

    const struct type *rhs_type;
    type_t t;
    uint8_t scalar;

    if (uexp->op != LEX_TK_SZOF && uexp->op != LEX_TK_ALOF) {
        rhs_type = type_from_expr(uexp->rhs, scope);

        if (!rhs_type) {
            goto fail_free;
        }

        if (unlikely(type_copy(uexp->type, rhs_type))) {
            (void) NOMEM;
            goto fail_free;
        }

        t = rhs_type->t;
        scalar = rhs_type->count == 1;
    }

    switch (uexp->op) {
    case LEX_TK_PLUS: {
        if (!scalar) {
            INVALID("unary plus requires a scalar value", uexp->rhs);
            goto fail_free;
        }

        if (!type_is_integral(t)) {
            INVALID("unary plus to non-integral expr", uexp->rhs);
            goto fail_free;
        }
    } break;

    case LEX_TK_MINS: {
        if (!scalar) {
            INVALID("unary minus requires a scalar value", uexp->rhs);
            goto fail_free;
        }

        if (!type_is_integral(t)) {
            INVALID("unary minus to non-integral expr", uexp->rhs);
            goto fail_free;
        }

        if (type_is_unsigned(t)) {
            uexp->type->t++;
        }
    } break;

    case LEX_TK_EXCL: {
        if (!scalar) {
            INVALID("unary not requires a scalar value", uexp->rhs);
            goto fail_free;
        }

        if (!type_is_integral(t) && !type_is_ptr(t) && !type_is_quaint(t)) {
            INVALID("unary not to non-integral expr", uexp->rhs);
            goto fail_free;
        }
    } break;

    case LEX_TK_TILD: {
        struct type *const quaint = type_alloc;

        if (unlikely(!quaint)) {
            (void) NOMEM;
            goto fail_free;
        }

        quaint->t = TYPE_QUAINT;
        quaint->count = 1;
        quaint->subtype = uexp->type;
        uexp->type = quaint;
    } break;

    case LEX_TK_MULT: {
        if (type_is_quaint(t) && !expr_is_lvalue(uexp->rhs)) {
            goto fail_free;
        }

        if (!scalar) {
            INVALID("unary star requires a scalar pointer/quaint", uexp->rhs);
            goto fail_free;
        }

        if (!type_is_ptr(t) && !type_is_quaint(t)) {
            INVALID("unary star to non-pointer, non-quaint", uexp->rhs);
            goto fail_free;
        }

        if (t == TYPE_VPTR) {
            INVALID("unary star to void pointer", uexp->rhs);
            goto fail_free;
        }

        if (t == TYPE_FPTR) {
            INVALID("unary star to function pointer", uexp->rhs);
            goto fail_free;
        }

        struct type *const outer = uexp->type;
        uexp->type = uexp->type->subtype;
        free(outer);
    } break;

    case LEX_TK_AMPS: {
        if (!expr_is_lvalue(uexp->rhs)) {
            goto fail_free;
        }

        struct type *const ptr = type_alloc;

        if (unlikely(!ptr)) {
            (void) NOMEM;
            goto fail_free;
        }

        ptr->t = TYPE_PTR;
        ptr->count = 1;
        ptr->subtype = uexp->type;
        uexp->type = ptr;
    } break;

    case LEX_TK_CARE: {
        if (!scalar) {
            INVALID("bitwise negation requires a scalar value", node);
            goto fail_free;
        }

        if (!type_is_integral(t)) {
            INVALID("bitwise negation to non-integral expr", node);
            goto fail_free;
        }

        if (type_is_signed(t)) {
            INVALID("bitwise negation to signed expr", node);
            goto fail_free;
        }
    } break;

    case LEX_TK_INCR:
    case LEX_TK_DECR: {
        if (!expr_is_lvalue(uexp->rhs)) {
            goto fail_free;
        }

        if (!scalar) {
            INVALID("prefix inc/dec requires a scalar value", node);
            goto fail_free;
        }

        if (!type_is_integral(t) && !type_is_ptr(t)) {
            INVALID("prefix inc/dec to non-integral expr", node);
            goto fail_free;
        }

        if (t == TYPE_VPTR) {
            INVALID_NULL("arithmetic on void pointer", node);
            goto fail_free;
        }

        if (t == TYPE_FPTR) {
            INVALID_NULL("arithmetic on function pointer", node);
            goto fail_free;
        }
    } break;

    case LEX_TK_SZOF:
    case LEX_TK_ALOF: {
        uexp->type->t = TYPE_USIZE;
        uexp->type->count = 1;
    } break;

    default: assert(0), abort();
    }

    return uexp->type;

fail_free:
    return type_free(uexp->type), uexp->type = NULL;
}

static const struct type *type_from_fexp(const struct ast_node *const node,
    const struct scope *const scope)
{
    struct ast_fexp *const fexp = ast_data(node, fexp);
    assert(fexp->type == NULL && fexp->arg_count == 0);
    const struct type *const lhs_type = type_from_expr(fexp->lhs, scope);

    if (!lhs_type) {
        return NULL;
    }

    if (lhs_type->t != TYPE_FPTR) {
        return INVALID_NULL("fexp lhs must be of type fptr", fexp->lhs);
    }

    if (lhs_type->count != 1) {
        return INVALID_NULL("fexp lhs must be scalar", fexp->lhs);
    }

    const struct ast_node *arglist = fexp->rhs;

    while (arglist) {
        ++fexp->arg_count;

        if (fexp->arg_count > lhs_type->param_count) {
            return INVALID_NULL("excessive argument count", node);
        }

        const struct ast_node *arg;

        if (arglist->an == AST_AN_BEXP && ast_data(arglist, bexp)->op == LEX_TK_COMA) {
            arg = ast_data(arglist, bexp)->lhs;
            arglist = ast_data(arglist, bexp)->rhs;
        } else {
            arg = arglist;
            arglist = NULL;
        }

        const struct type *const argtype = type_from_expr(arg, scope);

        if (!argtype) {
            return NULL;
        }

        const struct type *const paramtype =
            lhs_type->params[fexp->arg_count - 1].type;

        if (!type_equals(argtype, paramtype)) {
            return INVALID_NULL("arg does not match param type", arg);
        }
    }

    if (fexp->arg_count != lhs_type->param_count) {
        return INVALID_NULL("wrong argument count", node);
    }

    if (unlikely(!(fexp->type = type_alloc))) {
        return NOMEM_NULL;
    }

    if (lhs_type->rettype && unlikely(type_copy(fexp->type, lhs_type->rettype))) {
        return type_free(fexp->type), fexp->type = NULL, NOMEM_NULL;
    }

    return fexp->type;
}

static const struct type *type_from_xexp(const struct ast_node *const node,
    const struct scope *const scope)
{
    struct ast_xexp *const xexp = ast_data(node, xexp);
    assert(xexp->type == NULL);
    const struct type *const lhs_type = type_from_expr(xexp->lhs, scope);

    if (!lhs_type) {
        return NULL;
    }

    if (unlikely(!(xexp->type = type_alloc))) {
        return NOMEM_NULL;
    }

    if (unlikely(type_copy(xexp->type, lhs_type))) {
        (void) NOMEM;
        goto fail_free;
    }

    const type_t t = lhs_type->t;
    const uint8_t scalar = lhs_type->count == 1;

    switch (xexp->op) {
    case LEX_TK_INCR:
    case LEX_TK_DECR: {
        if (!expr_is_lvalue(xexp->lhs)) {
            goto fail_free;
        }

        if (!scalar) {
            INVALID("postfix inc/dec requires a scalar value", node);
            goto fail_free;
        }

        if (!type_is_integral(t) && !type_is_ptr(t)) {
            INVALID("postfix inc/dec to non-integral expr", node);
            goto fail_free;
        }

        if (t == TYPE_VPTR) {
            INVALID_NULL("arithmetic on void pointer", node);
            goto fail_free;
        }

        if (t == TYPE_FPTR) {
            INVALID_NULL("arithmetic on function pointer", node);
            goto fail_free;
        }
    } break;

    default: assert(0), abort();
    }

    return xexp->type;

fail_free:
    return type_free(xexp->type), xexp->type = NULL;
}

static const struct type *type_from_aexp(const struct ast_node *const node,
    const struct scope *const scope)
{
    struct ast_aexp *const aexp = ast_data(node, aexp);
    assert(aexp->type == NULL);
    const struct type *const base_type = type_from_expr(aexp->base, scope);
    const struct type *const off_type = type_from_expr(aexp->off, scope);

    if (!base_type || !off_type) {
        return NULL;
    }

    if (base_type->count == 1) {
        return INVALID_NULL("subscripted object is not an array", node);
    }

    if (!type_is_integral(off_type->t)) {
        return INVALID_NULL("non-integral array offset", aexp->off);
    }

    if (type_is_signed(off_type->t)) {
        return INVALID_NULL("signed array offset", aexp->off);
    }

    if (unlikely(!(aexp->type = type_alloc))) {
        return NOMEM_NULL;
    }

    if (unlikely(type_copy(aexp->type, base_type))) {
        return type_free(aexp->type), aexp->type = NULL, NOMEM_NULL;
    }

    aexp->type->count = 1;
    return aexp->type;
}

static const struct type *type_from_texp(const struct ast_node *const node,
    const struct scope *const scope)
{
    struct ast_texp *const texp = ast_data(node, texp);
    assert(texp->type == NULL);

    const struct type
        *const cond_type = type_from_expr(texp->cond, scope),
        *const tval_type = type_from_expr(texp->tval, scope),
        *const fval_type = type_from_expr(texp->fval, scope);

    if (!cond_type || !tval_type || !fval_type) {
        return NULL;
    }

    const type_t t = cond_type->t;

    if (!type_is_integral(t) && !type_is_ptr(t) && !type_is_quaint(t)) {
        return INVALID_NULL("non-integral texp cond", texp->cond);
    }

    if (!type_equals(tval_type, fval_type)) {
        return INVALID_NULL("differing types in texp", node);
    }

    if (unlikely(!(texp->type = type_alloc))) {
        return NOMEM_NULL;
    }

    if (unlikely(type_copy(texp->type, tval_type))) {
        return type_free(texp->type), texp->type = NULL, NOMEM_NULL;
    }

    return texp->type;
}

static const struct type *type_from_expr(const struct ast_node *const node,
    const struct scope *const scope)
{
    if (unlikely(!node)) {
        /* must have failed in validate_expr() */
        return NULL;
    }

    const struct type *expr_type;

    switch (node->an) {
    case AST_AN_VOID:
        return NULL;

    case AST_AN_BEXP: expr_type = type_from_bexp(node, scope); break;
    case AST_AN_UEXP: expr_type = type_from_uexp(node, scope); break;
    case AST_AN_FEXP: expr_type = type_from_fexp(node, scope); break;
    case AST_AN_XEXP: expr_type = type_from_xexp(node, scope); break;
    case AST_AN_AEXP: expr_type = type_from_aexp(node, scope); break;
    case AST_AN_TEXP: expr_type = type_from_texp(node, scope); break;

    case AST_AN_NAME: expr_type = type_from_name(node, scope); break;

    case AST_AN_NMBR: {
        struct ast_nmbr *const nmbr = ast_data(node, nmbr);
        assert(nmbr->type == NULL);
        return nmbr->type =
            (nmbr->value <= 0x000000FF ? u8  :
            (nmbr->value <= 0x0000FFFF ? u16 :
            (nmbr->value <= 0xFFFFFFFF ? u32 : u64)));
    }

    case AST_AN_STRL: {
        struct ast_strl *const strl = ast_data(node, strl);
        assert(strl->type == NULL);
        return strl->type = u8ptr;
    }

    default: assert(0), abort();
    }

    if (likely(expr_type)) {
        return unlikely(type_quantify((struct type *) expr_type)) ? NULL : expr_type;
    } else {
        return NULL;
    }
}

static int check_block(const struct ast_node *);

static int check_decl(const struct ast_node *const stmt,
    const struct scope *const scope)
{
    int error = TYPE_OK;
    const struct ast_decl *const decl = ast_data(stmt, decl);

    if (decl->init_expr) {
        const struct type *init_type;

        if (!(init_type = type_from_expr(decl->init_expr, scope))) {
            error = TYPE_INVALID;
        } else if (!type_equals(decl->type, init_type)) {
            error = INVALID("init type does not match decl type", stmt);
        }
    }

    if (unlikely(type_quantify(decl->type))) {
        error = TYPE_INVALID;
    }

    return error;
}

static int check_cond(const struct ast_node *const stmt,
    const struct scope *const scope)
{
    int error = TYPE_OK;
    const struct ast_cond *const cond = ast_data(stmt, cond);

    if (!type_from_expr(cond->if_expr, scope)) {
        error = TYPE_INVALID;
    }

    if (check_block(cond->if_block)) {
        error = TYPE_INVALID;
    }

    for (size_t idx = 0; idx < cond->elif_count; ++idx) {
        if (!type_from_expr(cond->elif[idx].expr, scope)) {
            error = TYPE_INVALID;
        }

        if (check_block(cond->elif[idx].block)) {
            error = TYPE_INVALID;
        }
    }

    if (cond->else_block && check_block(cond->else_block)) {
        error = TYPE_INVALID;
    }

    return error;
}

static int check_blok(const struct ast_node *const stmt,
    const struct scope *const scope)
{
    int error = TYPE_OK;

    if (stmt->an == AST_AN_WHIL && !type_from_expr(ast_data(stmt, whil)->expr, scope)) {
        error = TYPE_INVALID;
    }

    if (check_block(stmt)) {
        error = TYPE_INVALID;
    }

    if (stmt->an == AST_AN_DOWH && !type_from_expr(ast_data(stmt, dowh)->expr, scope)) {
        error = TYPE_INVALID;
    }

    return error;
}

static int check_retn(const struct ast_node *const stmt,
    const struct scope *const scope, const struct ast_func *const outer_func)
{
    int error = TYPE_OK;
    const struct ast_retn *const retn = ast_data(stmt, retn);

    if (retn->expr && outer_func->rettype) {
        const struct type *const expr_type = type_from_expr(retn->expr, scope);

        if (!expr_type) {
            error = TYPE_INVALID;
        } else if (!type_equals(outer_func->rettype, expr_type)) {
            error = INVALID("return type does not match func type", stmt);
        }
    } else if (retn->expr && !outer_func->rettype) {
        error = INVALID("returning a value in void func", stmt);
    } else if (!retn->expr && outer_func->rettype) {
        error = INVALID("return requires a value in a non-void func", stmt);
    }

    return error;
}

static int check_wait(const struct ast_node *const stmt,
    const struct scope *const scope)
{
    int error = TYPE_OK;
    struct ast_wait *const wait = ast_data(stmt, wait);
    const struct type *const wtype = type_from_expr(wait->wquaint, scope);

    if (!wtype) {
        error = TYPE_INVALID;
    } else if (wtype->t != TYPE_QUAINT) {
        error = INVALID("wait needs quaint type", wait->wquaint);
    }

    if (wait->wfor) {
        const struct type *const ftype = type_from_expr(wait->wfor, scope);

        if (!ftype) {
            error = TYPE_INVALID;
        } else if (ftype->count != 1) {
            error = INVALID("wait-for requires a scalar value", wait->wfor);
        } else if (!type_is_integral(ftype->t) || type_is_signed(ftype->t)) {
            error = INVALID("wait-for requires an unsigned value", wait->wfor);
        }
    } else if (wait->wunt) {
        if (wait->wunt->an != AST_AN_BEXP) {
            return INVALID("wait-until requires a label", wait->wunt);
        }

        const struct ast_bexp *const bexp = ast_data(wait->wunt, bexp);

        if (bexp->op != LEX_TK_SCOP) {
            return INVALID("invalid wait-until label", wait->wunt);
        }

        const struct ast_node *const lhs = bexp->lhs;
        const struct ast_node *const rhs = bexp->rhs;

        if (lhs->an != AST_AN_NAME || rhs->an != AST_AN_NAME) {
            return INVALID("invalid wait-until label", wait->wunt);
        }

        struct scope_obj *found = scope_find_object(unit_scope,
            (const struct lex_symbol *) lhs->ltok);

        if (!found || found->obj != SCOPE_OBJ_FUNC) {
            return INVALID("no such function", wait->wunt);
        }

        const struct ast_func *const func = ast_data(found->func, func);
        const ptrdiff_t index = scope_find_wlab(func,
            (const struct lex_symbol *) rhs->ltok);

        if (index == -1) {
            return INVALID("no such label in function", wait->wunt);
        }

        wait->func = func;
        wait->wlab_idx = (size_t) index;
    }

    return error;
}

static void identify_wlab(const struct ast_node *const stmt,
    const struct ast_func *const func)
{
    struct ast_wlab *const wlab = ast_data(stmt, wlab);
    assert(wlab->func == 0);
    assert(wlab->id == 0);
    const ptrdiff_t wlab_idx = scope_find_wlab(func, wlab->name);
    assert(wlab_idx != -1);
    wlab->func = (uintptr_t) func;
    wlab->id = func->wlabs[wlab_idx].id;
    assert(wlab->func != 0);
    assert(wlab->id != 0);
}

static int check_block(const struct ast_node *const node)
{
    const struct scope *scope;
    struct ast_node *const *stmts;
    size_t stmt_count;
    static const struct ast_func *outer_func;

    assert(node != NULL);
    int error = TYPE_OK;

    switch (node->an) {
    case AST_AN_FUNC: {
        const struct ast_func *const func = ast_data(node, func);

        for (size_t idx = 0; idx < func->param_count; ++idx) {
            if (unlikely(type_quantify(func->params[idx].type))) {
                error = ((void) NOMEM, TYPE_INVALID);
            }
        }

        scope = func->scope;
        stmts = func->stmts;
        stmt_count = func->stmt_count;
        outer_func = func;
    } break;

    case AST_AN_BLOK:
    case AST_AN_NOIN: {
        const struct ast_blok *const blok = ast_data(node, blok);
        scope = blok->scope;
        stmts = blok->stmts;
        stmt_count = blok->stmt_count;
    } break;

    case AST_AN_WHIL:
    case AST_AN_DOWH: {
        const struct ast_whil *const whil = ast_data(node, whil);
        scope = whil->scope;
        stmts = whil->stmts;
        stmt_count = whil->stmt_count;
    } break;

    default:
        assert(0), abort();
    }

    for (size_t idx = 0; idx < stmt_count; ++idx) {
        struct ast_node *const stmt = stmts[idx];

        if (unlikely(!stmt)) {
            continue;
        }

        switch (stmt->an) {
        case AST_AN_VOID:
        case AST_AN_TYPE:
        case AST_AN_FUNC:
            /* must have failed in ast_build() */
            break;

        case AST_AN_DECL:
            check_decl(stmt, scope) && (error = TYPE_INVALID);
            break;

        case AST_AN_COND:
            check_cond(stmt, scope) && (error = TYPE_INVALID);
            break;

        case AST_AN_BLOK:
        case AST_AN_NOIN:
        case AST_AN_WHIL:
        case AST_AN_DOWH:
            check_blok(stmt, scope) && (error = TYPE_INVALID);
            break;

        case AST_AN_RETN:
            check_retn(stmt, scope, outer_func) && (error = TYPE_INVALID);
            break;

        case AST_AN_WAIT:
            check_wait(stmt, scope) && (error = TYPE_INVALID);
            break;

        case AST_AN_WLAB:
            identify_wlab(stmt, outer_func);
            break;

        case AST_AN_BEXP:
        case AST_AN_UEXP:
        case AST_AN_FEXP:
        case AST_AN_XEXP:
        case AST_AN_AEXP:
        case AST_AN_TEXP:
        case AST_AN_NAME:
        case AST_AN_NMBR:
        case AST_AN_STRL:
            !type_from_expr(stmt, scope) && (error = TYPE_INVALID);
            break;

        default: assert(0), abort();
        }
    }

    return error;
}

int type_check_ast(const struct ast_node *const root)
{
    assert(root != NULL);

    const struct ast_unit *const unit = ast_data(root, unit);
    int error = TYPE_OK;
    unit_scope = unit->scope;

    struct type *const types_to_quantify[] = {
        u8, u16, u32, u64, u8ptr,
    };

    for (size_t idx = 0; idx < countof(types_to_quantify); ++idx) {
        type_quantify(types_to_quantify[idx]);
    }

    for (size_t idx = 0; idx < unit->stmt_count; ++idx) {
        const struct ast_node *const stmt = unit->stmts[idx];

        if (unlikely(!stmt)) {
            continue;
        }

        switch (stmt->an) {
        case AST_AN_VOID:
        case AST_AN_BLOK:
        case AST_AN_NOIN:
        case AST_AN_COND:
        case AST_AN_WHIL:
        case AST_AN_DOWH:
        case AST_AN_RETN:
        case AST_AN_WAIT:
        case AST_AN_WLAB:
            /* must have failed in ast_build() */
            break;

        case AST_AN_TYPE:
            break;

        case AST_AN_DECL:
            check_decl(stmt, unit->scope) && (error = TYPE_INVALID);
            break;

        case AST_AN_FUNC:
            check_block(stmt) && (error = TYPE_INVALID);
            break;

        case AST_AN_BEXP:
        case AST_AN_UEXP:
        case AST_AN_FEXP:
        case AST_AN_XEXP:
        case AST_AN_AEXP:
        case AST_AN_TEXP:
        case AST_AN_NAME:
        case AST_AN_NMBR:
        case AST_AN_STRL:
            error = INVALID("invalid statement in unit context", stmt);
            break;

        default: assert(0), abort();
        }
    }

    return error;
}
