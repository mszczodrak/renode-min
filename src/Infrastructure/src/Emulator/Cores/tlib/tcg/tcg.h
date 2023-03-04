/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef TCG_H
#define TCG_H

/// INSTEAD OF QEMU-COMMON
#ifndef TARGET_PAGE_BITS
extern int TARGET_PAGE_BITS;
#endif

#ifndef TARGET_LONG_BITS
#   error "TARGET_LONG_BITS not defined"
#elif TARGET_LONG_BITS != 32 && TARGET_LONG_BITS != 64
#   error "Only 32 or 64 values are supported for TARGET_LONG_BITS"
#endif

#ifndef HOST_LONG_BITS
#   error "HOST_LONG_BITS not defined"
#elif HOST_LONG_BITS != 32 && HOST_LONG_BITS != 64
#   error "Only 32 or 64 values are supported for HOST_LONG_BITS"
#endif

#define TARGET_PAGE_SIZE      (1ull << TARGET_PAGE_BITS)
#define TARGET_PAGE_MASK      ~(TARGET_PAGE_SIZE - 1)

#define TARGET_LONG_ALIGNMENT 4

#if TARGET_LONG_BITS == 32
typedef uint32_t target_ulong __attribute__((aligned(TARGET_LONG_ALIGNMENT)));
#elif TARGET_LONG_BITS == 64
typedef uint64_t target_ulong __attribute__((aligned(TARGET_LONG_ALIGNMENT)));
#endif

#if HOST_LONG_BITS == 32 && TARGET_LONG_BITS == 32
#define CPU_TLB_ENTRY_BITS  4
#else
#define CPU_TLB_ENTRY_BITS  5
#endif

#define CPU_TEMP_BUF_NLONGS 128
#define TCG_TARGET_REG_BITS HOST_LONG_BITS

#define CPU_TLB_BITS        8
#define CPU_TLB_SIZE        (1 << CPU_TLB_BITS)

//// END

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "additional.h"
#include "tcg-target.h"
#include "tcg-runtime.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#if TCG_TARGET_REG_BITS == 32
typedef int32_t tcg_target_long;
typedef uint32_t tcg_target_ulong;
#define TCG_PRIlx PRIX32
#define TCG_PRIld PRId32
#elif TCG_TARGET_REG_BITS == 64
typedef int64_t tcg_target_long;
typedef uint64_t tcg_target_ulong;
#define TCG_PRIlx PRIX64
#define TCG_PRIld PRId64
#else
#error unsupported
#endif

#if TCG_TARGET_NB_REGS <= 32
typedef uint32_t TCGRegSet;
#elif TCG_TARGET_NB_REGS <= 64
typedef uint64_t TCGRegSet;
#else
#error unsupported
#endif

/* Turn some undef macros into false macros.  */
#if TCG_TARGET_REG_BITS == 32
#define TCG_TARGET_HAS_div_i64     0
#define TCG_TARGET_HAS_div2_i64    0
#define TCG_TARGET_HAS_rot_i64     0
#define TCG_TARGET_HAS_ext8s_i64   0
#define TCG_TARGET_HAS_ext16s_i64  0
#define TCG_TARGET_HAS_ext32s_i64  0
#define TCG_TARGET_HAS_ext8u_i64   0
#define TCG_TARGET_HAS_ext16u_i64  0
#define TCG_TARGET_HAS_ext32u_i64  0
#define TCG_TARGET_HAS_bswap16_i64 0
#define TCG_TARGET_HAS_bswap32_i64 0
#define TCG_TARGET_HAS_bswap64_i64 0
#define TCG_TARGET_HAS_neg_i64     0
#define TCG_TARGET_HAS_not_i64     0
#define TCG_TARGET_HAS_andc_i64    0
#define TCG_TARGET_HAS_orc_i64     0
#define TCG_TARGET_HAS_eqv_i64     0
#define TCG_TARGET_HAS_nand_i64    0
#define TCG_TARGET_HAS_nor_i64     0
#define TCG_TARGET_HAS_deposit_i64 0
#define TCG_TARGET_HAS_mulu2_i64   0
#define TCG_TARGET_HAS_muls2_i64   0
/* Turn some undef macros into true macros.  */
#define TCG_TARGET_HAS_mulu2_i32   1
#endif

#ifndef TCG_TARGET_deposit_i32_valid
#define TCG_TARGET_deposit_i32_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_deposit_i64_valid
#define TCG_TARGET_deposit_i64_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_extract_i32_valid
#define TCG_TARGET_extract_i32_valid(ofs, len) 1
#endif

/* Only one of DIV or DIV2 should be defined.  */
#if defined(TCG_TARGET_HAS_div_i32)
#define TCG_TARGET_HAS_div2_i32 0
#elif defined(TCG_TARGET_HAS_div2_i32)
#define TCG_TARGET_HAS_div_i32  0
#endif
#if defined(TCG_TARGET_HAS_div_i64)
#define TCG_TARGET_HAS_div2_i64 0
#elif defined(TCG_TARGET_HAS_div2_i64)
#define TCG_TARGET_HAS_div_i64  0
#endif

#ifndef TARGET_INSN_START_EXTRA_WORDS
#define TARGET_INSN_START_WORDS 1
#else
#define TARGET_INSN_START_WORDS (1 + TARGET_INSN_START_EXTRA_WORDS)
#endif
typedef enum TCGOpcode {
#define DEF(name, oargs, iargs, cargs, flags) INDEX_op_ ## name,
#include "tcg-opc.h"
#undef DEF
    NB_OPS,
} TCGOpcode;

#define tcg_regset_clear(d)             (d) = 0
#define tcg_regset_set(d, s)            (d) = (s)
#define tcg_regset_set32(d, reg, val32) (d) |= (val32) << (reg)
#define tcg_regset_set_reg(d, r)        (d) |= 1L << (r)
#define tcg_regset_reset_reg(d, r)      (d) &= ~(1L << (r))
#define tcg_regset_test_reg(d, r)       (((d) >> (r)) & 1)
#define tcg_regset_or(d, a, b)          (d) = (a) | (b)
#define tcg_regset_and(d, a, b)         (d) = (a) & (b)
#define tcg_regset_andnot(d, a, b)      (d) = (a) & ~(b)
#define tcg_regset_not(d, a)            (d) = ~(a)

typedef struct TCGRelocation {
    struct TCGRelocation *next;
    int type;
    uint8_t *ptr;
    tcg_target_long addend;
} TCGRelocation;

typedef struct TCGLabel {
    int has_value;
    union {
        tcg_target_ulong value;
        TCGRelocation *first_reloc;
    } u;
} TCGLabel;

typedef struct TCGPool {
    struct TCGPool *next;
    int size;
    uint8_t data[0] __attribute__ ((aligned));
} TCGPool;

#define TCG_POOL_CHUNK_SIZE       32768

#define TCG_MAX_LABELS            512

#define TCG_MAX_TEMPS             512

#define TCG_MAX_INSNS             10000

/* when the size of the arguments of a called function is smaller than
   this value, they are statically allocated in the TB stack frame */
#define TCG_STATIC_CALL_ARGS_SIZE 128

typedef enum TCGType {
    TCG_TYPE_I32,
    TCG_TYPE_I64,
    TCG_TYPE_COUNT, /* number of different types */

    /* An alias for the size of the host register.  */
#if TCG_TARGET_REG_BITS == 32
    TCG_TYPE_REG = TCG_TYPE_I32,
#else
    TCG_TYPE_REG = TCG_TYPE_I64,
#endif

    /* An alias for the size of the native pointer.  We don't currently
       support any hosts with 64-bit registers and 32-bit pointers.  */
    TCG_TYPE_PTR = TCG_TYPE_REG,

    /* An alias for the size of the target "long", aka register.  */
#if TARGET_LONG_BITS == 64
    TCG_TYPE_TL  = TCG_TYPE_I64,
#else
    TCG_TYPE_TL  = TCG_TYPE_I32,
#endif
} TCGType;

typedef tcg_target_ulong TCGArg;

/* Define a type and accessor macros for variables.  Using a struct is
   nice because it gives some level of type safely.  Ideally the compiler
   be able to see through all this.  However in practice this is not true,
   expecially on targets with braindamaged ABIs (e.g. i386).
   We use plain int by default to avoid this runtime overhead.
   Users of tcg_gen_* don't need to know about any of this, and should
   treat TCGv as an opaque type.
   In addition we do typechecking for different types of variables.  TCGv_i32
   and TCGv_i64 are 32/64-bit variables respectively.  TCGv and TCGv_ptr
   are aliases for target_ulong and host pointer sized values respectively.
 */

typedef int TCGv_i32;
typedef int TCGv_i64;
#if TCG_TARGET_REG_BITS == 32
#define TCGv_ptr TCGv_i32
#else
#define TCGv_ptr TCGv_i64
#endif
typedef TCGv_ptr TCGv_env;
#define MAKE_TCGV_I32(x)     (x)
#define MAKE_TCGV_I64(x)     (x)
#define MAKE_TCGV_PTR(x)     (x)
#define GET_TCGV_I32(t)      (t)
#define GET_TCGV_I64(t)      (t)
#define GET_TCGV_PTR(t)      (t)

#if TCG_TARGET_REG_BITS == 32
#define TCGV_LOW(t)          (t)
#define TCGV_HIGH(t)         ((t) + 1)
#endif

#define TCGV_EQUAL_I32(a, b) (GET_TCGV_I32(a) == GET_TCGV_I32(b))
#define TCGV_EQUAL_I64(a, b) (GET_TCGV_I64(a) == GET_TCGV_I64(b))

/* Dummy definition to avoid compiler warnings.  */
#define TCGV_UNUSED_I32(x)   x = MAKE_TCGV_I32(-1)
#define TCGV_UNUSED_I64(x)   x = MAKE_TCGV_I64(-1)

/* call flags */
#define TCG_CALL_TYPE_MASK      0x000f
#define TCG_CALL_TYPE_STD       0x0000 /* standard C call */
#define TCG_CALL_TYPE_REGPARM_1 0x0001 /* i386 style regparm call (1 reg) */
#define TCG_CALL_TYPE_REGPARM_2 0x0002 /* i386 style regparm call (2 regs) */
#define TCG_CALL_TYPE_REGPARM   0x0003 /* i386 style regparm call (3 regs) */
/* A pure function only reads its arguments and TCG global variables
   and cannot raise exceptions. Hence a call to a pure function can be
   safely suppressed if the return value is not used. */
#define TCG_CALL_PURE           0x0010
/* A const function only reads its arguments and does not use TCG
   global variables. Hence a call to such a function does not
   save TCG global variables back to their canonical location. */
#define TCG_CALL_CONST          0x0020

/* used to align parameters */
#define TCG_CALL_DUMMY_TCGV     MAKE_TCGV_I32(-1)
#define TCG_CALL_DUMMY_ARG      ((TCGArg)(-1))

/* Conditions.  Note that these are laid out for easy manipulation by
   the functions below:
     bit 0 is used for inverting;
     bit 1 is signed,
     bit 2 is unsigned,
     bit 3 is used with bit 0 for swapping signed/unsigned.  */
typedef enum {
    /* non-signed */
    TCG_COND_NEVER  = 0 | 0 | 0 | 0,
    TCG_COND_ALWAYS = 0 | 0 | 0 | 1,
    TCG_COND_EQ     = 8 | 0 | 0 | 0,
    TCG_COND_NE     = 8 | 0 | 0 | 1,
    /* signed */
    TCG_COND_LT     = 0 | 0 | 2 | 0,
    TCG_COND_GE     = 0 | 0 | 2 | 1,
    TCG_COND_LE     = 8 | 0 | 2 | 0,
    TCG_COND_GT     = 8 | 0 | 2 | 1,
    /* unsigned */
    TCG_COND_LTU    = 0 | 4 | 0 | 0,
    TCG_COND_GEU    = 0 | 4 | 0 | 1,
    TCG_COND_LEU    = 8 | 4 | 0 | 0,
    TCG_COND_GTU    = 8 | 4 | 0 | 1,
} TCGCond;

/* Invert the sense of the comparison.  */
static inline TCGCond tcg_invert_cond(TCGCond c)
{
    return (TCGCond)(c ^ 1);
}

/* Swap the operands in a comparison.  */
static inline TCGCond tcg_swap_cond(TCGCond c)
{
    int mask = (c < TCG_COND_LT ? 0 : c < TCG_COND_LTU ? 7 : 15);
    return (TCGCond)(c ^ mask);
}

static inline TCGCond tcg_unsigned_cond(TCGCond c)
{
    return (c >= TCG_COND_LT && c <= TCG_COND_GT ? c + 4 : c);
}

#define TEMP_VAL_DEAD  0
#define TEMP_VAL_REG   1
#define TEMP_VAL_MEM   2
#define TEMP_VAL_CONST 3

/* XXX: optimize memory layout */
typedef struct TCGTemp {
    TCGType base_type;
    TCGType type;
    int val_type;
    int reg;
    tcg_target_long val;
    int mem_reg;
    tcg_target_long mem_offset;
    unsigned int fixed_reg : 1;
    unsigned int mem_coherent : 1;
    unsigned int mem_allocated : 1;
    unsigned int temp_local : 1;     /* If true, the temp is saved across
                                        basic blocks. Otherwise, it is not
                                        preserved across basic blocks. */
    unsigned int temp_allocated : 1; /* never used for code gen */
    /* index of next free temp of same base type, -1 if end */
    int next_free_temp;
    const char *name;
} TCGTemp;

typedef struct TCGHelperInfo {
    tcg_target_ulong func;
    const char *name;
} TCGHelperInfo;

typedef struct TCGContext TCGContext;

struct TCGContext {
    uint8_t *pool_cur, *pool_end;
    TCGPool *pool_first, *pool_current;
    TCGLabel *labels;
    int nb_labels;
    TCGTemp *temps; /* globals first, temps after */
    int nb_globals;
    int nb_temps;
    /* index of free temps, -1 if none */
    int first_free_temp[TCG_TYPE_COUNT * 2];

    /* goto_tb support */
    uint8_t *code_buf;
    uintptr_t *tb_next;
    uint16_t *tb_next_offset;
    uint16_t *tb_jmp_offset;

    /* liveness analysis */
    uint16_t *op_dead_args; /* for each operation, each bit tells if the
                               corresponding argument is dead */

    /* tells in which temporary a given register is. It does not take
       into account fixed registers */
    int reg_to_temp[TCG_TARGET_NB_REGS];
    TCGRegSet reserved_regs;
    tcg_target_long current_frame_offset;
    tcg_target_long frame_start;
    tcg_target_long frame_end;
    int frame_reg;

    uint8_t *code_ptr;
    TCGTemp static_temps[TCG_MAX_TEMPS];

    TCGHelperInfo *helpers;
    int nb_helpers;
    int allocated_helpers;
    int helpers_sorted;
    /* sets whether we should use the tlb in accesses */
    uint8_t use_tlb;
};

extern uint16_t *gen_opc_ptr;
extern TCGArg *gen_opparam_ptr;

/* pool based memory allocation */

void *tcg_malloc_internal(TCGContext *s, int size);
void tcg_pool_reset(TCGContext *s);
void tcg_pool_delete(TCGContext *s);

#if defined(__arm__)
/* The prologue must be reachable with a direct jump. ARM and Sparc64
   have limited branch ranges (possibly also PPC) so place it in a
   section close to code segment. */
#define code_gen_section                                \
    __attribute__((__section__(".gen_code")))           \
    __attribute__((aligned (32)))
#elif defined(_WIN32)
/* Maximum alignment for Win32 is 16. */
#define code_gen_section                                \
    __attribute__((aligned (16)))
#else
#define code_gen_section                                \
    __attribute__((aligned (32)))
#endif

typedef struct tcg_t {
    TCGContext *ctx;
    uint16_t *gen_opc_buf;
    TCGArg *gen_opparam_buf;
    uint8_t *code_gen_prologue;
    uint16_t *gen_insn_end_off;
    target_ulong (*gen_insn_data)[TARGET_INSN_START_WORDS];
    void *ldb;
    void *ldw;
    void *ldl;
    void *ldq;
    void *stb;
    void *stw;
    void *stl;
    void *stq;
} tcg_t;

extern tcg_t *tcg;

void tcg_attach(tcg_t *con);

static inline void *tcg_malloc(int size)
{
    TCGContext *s = tcg->ctx;
    uint8_t *ptr, *ptr_end;
    size = (size + sizeof(long) - 1) & ~(sizeof(long) - 1);
    ptr = s->pool_cur;
    ptr_end = ptr + size;
    if (unlikely(ptr_end > s->pool_end)) {
        return tcg_malloc_internal(tcg->ctx, size);
    } else {
        s->pool_cur = ptr_end;
        return ptr;
    }
}

void tcg_context_init();
void tcg_context_use_tlb(int value);
void tcg_dispose();
void tcg_prologue_init();
void tcg_func_start(TCGContext *s);

int tcg_gen_code(TCGContext *s, uint8_t *gen_code_buf);

void tcg_set_frame(TCGContext *s, int reg, tcg_target_long start, tcg_target_long size);

TCGv_i32 tcg_global_reg_new_i32(int reg, const char *name);
TCGv_i32 tcg_global_mem_new_i32(int reg, tcg_target_long offset, const char *name);
TCGv_i32 tcg_temp_new_internal_i32(int temp_local);
static inline TCGv_i32 tcg_temp_new_i32(void)
{
    return tcg_temp_new_internal_i32(0);
}
static inline TCGv_i32 tcg_temp_local_new_i32(void)
{
    return tcg_temp_new_internal_i32(1);
}
void tcg_temp_free_i32(TCGv_i32 arg);
char *tcg_get_arg_str_i32(TCGContext *s, char *buf, int buf_size, TCGv_i32 arg);

TCGv_i64 tcg_global_reg_new_i64(int reg, const char *name);
TCGv_i64 tcg_global_mem_new_i64(int reg, tcg_target_long offset, const char *name);
TCGv_i64 tcg_temp_new_internal_i64(int temp_local);
static inline TCGv_i64 tcg_temp_new_i64(void)
{
    return tcg_temp_new_internal_i64(0);
}
static inline TCGv_i64 tcg_temp_local_new_i64(void)
{
    return tcg_temp_new_internal_i64(1);
}
void tcg_temp_free_i64(TCGv_i64 arg);
char *tcg_get_arg_str_i64(TCGContext *s, char *buf, int buf_size, TCGv_i64 arg);

static inline bool tcg_arg_is_local(TCGContext *s, TCGArg arg)
{
    return s->temps[arg].temp_local;
}

#define tcg_clear_temp_count() do { } while (0)
#define tcg_check_temp_count() 0

#define TCG_CT_ALIAS  0x80
#define TCG_CT_IALIAS 0x40
#define TCG_CT_REG    0x01
#define TCG_CT_CONST  0x02 /* any constant of register size */

typedef struct TCGArgConstraint {
    uint16_t ct;
    uint8_t alias_index;
    union {
        TCGRegSet regs;
    } u;
} TCGArgConstraint;

#define TCG_MAX_OP_ARGS 16

/* Bits for TCGOpDef->flags, 8 bits available.  */
enum {
    /* Instruction defines the end of a basic block.  */
    TCG_OPF_BB_END       = 0x01,
    /* Instruction clobbers call registers and potentially update globals.  */
    TCG_OPF_CALL_CLOBBER = 0x02,
    /* Instruction has side effects: it cannot be removed
       if its outputs are not used.  */
    TCG_OPF_SIDE_EFFECTS = 0x04,
    /* Instruction operands are 64-bits (otherwise 32-bits).  */
    TCG_OPF_64BIT        = 0x08,
    /* Instruction is optional and not implemented by the host.  */
    TCG_OPF_NOT_PRESENT  = 0x10,
};

typedef struct TCGOpDef {
    const char *name;
    uint8_t nb_oargs, nb_iargs, nb_cargs, nb_args;
    uint8_t flags;
    TCGArgConstraint *args_ct;
    int *sorted_args;
} TCGOpDef;

extern TCGOpDef tcg_op_defs[];
extern const size_t tcg_op_defs_max;

typedef struct TCGTargetOpDef {
    TCGOpcode op;
    const char *args_ct_str[TCG_MAX_OP_ARGS];
} TCGTargetOpDef;

void tlib_abort(char *msg);

#define tcg_abort() \
do {\
    char msg[1024];\
    sprintf(msg, "%s:%d: tcg fatal error", __FILE__, __LINE__);\
    tlib_abort(msg); \
} while (0)

void tcg_add_target_add_op_defs(const TCGTargetOpDef *tdefs);

#if TCG_TARGET_REG_BITS == 32
#define TCGV_NAT_TO_PTR(n)   MAKE_TCGV_PTR(GET_TCGV_I32(n))
#define TCGV_PTR_TO_NAT(n)   MAKE_TCGV_I32(GET_TCGV_PTR(n))

#define tcg_const_ptr(V)     TCGV_NAT_TO_PTR(tcg_const_i32(V))
#define tcg_global_reg_new_ptr(R, N) \
    TCGV_NAT_TO_PTR(tcg_global_reg_new_i32((R), (N)))
#define tcg_global_mem_new_ptr(R, O, N) \
    TCGV_NAT_TO_PTR(tcg_global_mem_new_i32((R), (O), (N)))
#define tcg_temp_new_ptr()   TCGV_NAT_TO_PTR(tcg_temp_new_i32())
#define tcg_temp_free_ptr(T) tcg_temp_free_i32(TCGV_PTR_TO_NAT(T))
#else
#define TCGV_NAT_TO_PTR(n)   MAKE_TCGV_PTR(GET_TCGV_I64(n))
#define TCGV_PTR_TO_NAT(n)   MAKE_TCGV_I64(GET_TCGV_PTR(n))

#define tcg_const_ptr(V)     TCGV_NAT_TO_PTR(tcg_const_i64(V))
#define tcg_global_reg_new_ptr(R, N) \
    TCGV_NAT_TO_PTR(tcg_global_reg_new_i64((R), (N)))
#define tcg_global_mem_new_ptr(R, O, N) \
    TCGV_NAT_TO_PTR(tcg_global_mem_new_i64((R), (O), (N)))
#define tcg_temp_new_ptr()   TCGV_NAT_TO_PTR(tcg_temp_new_i64())
#define tcg_temp_free_ptr(T) tcg_temp_free_i64(TCGV_PTR_TO_NAT(T))
#endif

void tcg_gen_callN(TCGContext *s, TCGv_ptr func, unsigned int flags, int sizemask, TCGArg ret, int nargs, TCGArg *args);

void tcg_gen_shifti_i64(TCGv_i64 ret, TCGv_i64 arg1, int c, int right, int arith);

TCGArg *tcg_optimize(TCGContext *s, uint16_t *tcg_opc_ptr, TCGArg *args, TCGOpDef *tcg_op_def);

/* only used for debugging purposes */
void tcg_register_helper(void *func, const char *name);
const char *tcg_helper_get_name(TCGContext *s, void *func);
void tcg_dump_ops(TCGContext *s, FILE *outfile);

void dump_ops(const uint16_t *opc_buf, const TCGArg *opparam_buf);
TCGv_i32 tcg_const_i32(int32_t val);
TCGv_i64 tcg_const_i64(int64_t val);
TCGv_i32 tcg_const_local_i32(int32_t val);
TCGv_i64 tcg_const_local_i64(int64_t val);

/* Test for whether to terminate the TB for using too many opcodes.  */
static inline bool tcg_op_buf_full(void)
{
    //dummy implementation
    return 0;
}

/* Compute the current code size within the translation block. */
static inline size_t tcg_current_code_size(TCGContext *s)
{
    return (uintptr_t)s->code_ptr - (uintptr_t)s->code_buf;
}

/* TCG targets may use a different definition of tcg_tb_exec. */
#if !defined(tcg_tb_exec)
# define tcg_tb_exec(env, tb_ptr) \
    ((uintptr_t REGPARM (*)(void *, void *))tcg->code_gen_prologue)(env, tb_ptr)
#endif

/* Constants for qemu_ld and qemu_st for the Memory Operation field.  */
typedef enum TCGMemOp {
    MO_8     = 0,
    MO_16    = 1,
    MO_32    = 2,
    MO_64    = 3,
    MO_SIZE  = 3,   /* Mask for the above.  */

    MO_SIGN  = 4,   /* Sign-extended, otherwise zero-extended.  */

    MO_BSWAP = 8,   /* Host reverse endian.  */
#ifdef HOST_WORDS_BIGENDIAN
    MO_LE    = MO_BSWAP,
    MO_BE    = 0,
#else
    MO_LE    = 0,
    MO_BE    = MO_BSWAP,
#endif
#ifdef TARGET_WORDS_BIGENDIAN
    MO_TE    = MO_BE,
#else
    MO_TE    = MO_LE,
#endif

    /* MO_UNALN accesses are never checked for alignment.
     * MO_ALIGN accesses will result in a call to the CPU's
     * do_unaligned_access hook if the guest address is not aligned.
     * The default depends on whether the target CPU defines ALIGNED_ONLY.
     *
     * Some architectures (e.g. ARMv8) need the address which is aligned
     * to a size more than the size of the memory access.
     * Some architectures (e.g. SPARCv9) need an address which is aligned,
     * but less strictly than the natural alignment.
     *
     * MO_ALIGN supposes the alignment size is the size of a memory access.
     *
     * There are three options:
     * - unaligned access permitted (MO_UNALN).
     * - an alignment to the size of an access (MO_ALIGN);
     * - an alignment to a specified size, which may be more or less than
     *   the access size (MO_ALIGN_x where 'x' is a size in bytes);
     */
    MO_ASHIFT = 4,
    MO_AMASK = 7 << MO_ASHIFT,
#ifdef ALIGNED_ONLY
    MO_ALIGN = 0,
    MO_UNALN = MO_AMASK,
#else
    MO_ALIGN = MO_AMASK,
    MO_UNALN = 0,
#endif
    MO_ALIGN_2  = 1 << MO_ASHIFT,
    MO_ALIGN_4  = 2 << MO_ASHIFT,
    MO_ALIGN_8  = 3 << MO_ASHIFT,
    MO_ALIGN_16 = 4 << MO_ASHIFT,
    MO_ALIGN_32 = 5 << MO_ASHIFT,
    MO_ALIGN_64 = 6 << MO_ASHIFT,

    /* Combinations of the above, for ease of use.  */
    MO_UB    = MO_8,
    MO_UW    = MO_16,
    MO_UL    = MO_32,
    MO_SB    = MO_SIGN | MO_8,
    MO_SW    = MO_SIGN | MO_16,
    MO_SL    = MO_SIGN | MO_32,
    MO_Q     = MO_64,

    MO_LEUW  = MO_LE | MO_UW,
    MO_LEUL  = MO_LE | MO_UL,
    MO_LESW  = MO_LE | MO_SW,
    MO_LESL  = MO_LE | MO_SL,
    MO_LEQ   = MO_LE | MO_Q,

    MO_BEUW  = MO_BE | MO_UW,
    MO_BEUL  = MO_BE | MO_UL,
    MO_BESW  = MO_BE | MO_SW,
    MO_BESL  = MO_BE | MO_SL,
    MO_BEQ   = MO_BE | MO_Q,

    MO_TEUW  = MO_TE | MO_UW,
    MO_TEUL  = MO_TE | MO_UL,
    MO_TESW  = MO_TE | MO_SW,
    MO_TESL  = MO_TE | MO_SL,
    MO_TEQ   = MO_TE | MO_Q,

    MO_SSIZE = MO_SIZE | MO_SIGN,
} TCGMemOp;

#endif //TCG_H
