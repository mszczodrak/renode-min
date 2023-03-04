/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef XTENSA_CPU_H
#define XTENSA_CPU_H

#include "arch_callbacks.h"
#include "cpu-defs.h"
#include "host-utils.h"
#include "softfloat.h"
#include "xtensa-isa.h"

#define TARGET_LONG_BITS 32
#define TARGET_PAGE_BITS 12
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#define NB_MMU_MODES 4

#define assert(x) {if (unlikely(!(x))) tlib_abortf("Assert not met in %s:%d: %s", __FILE__, __LINE__, #x);}while(0)

/* Deposits 'length' bits from 'val' into 'dst_val' at 'start' bit. */
static inline uint32_t deposit32(uint32_t dst_val, uint8_t start, uint8_t length, uint32_t val)
{
    assert(start + length <= 32);
    // Number with only relevant bits ('start' to 'start+length-1') set.
    // 64-bit literals are used since '1 << 32' is a 33-bit value.
    uint32_t relevant_bits = ((1ull << length) - 1ull) << start;

    // Shift value into the start bit and reset the irrelevant bits.
    val = (val << start) & relevant_bits;

    // Reset the relevant bits in the destination value.
    dst_val &= ~relevant_bits;

    // Return the above values merged.
    return dst_val | val;
}

/* Extracts 'length' bits of 'value' at 'start' bit. */
static inline uint32_t extract32(uint32_t value, uint8_t start, uint8_t length)
{
    return (value >> start) & ((((uint32_t)1) << length) - 1);
}

/* Xtensa processors have a weak memory model */
#define TCG_GUEST_DEFAULT_MO      (0)

enum {
    /* Additional instructions */
    XTENSA_OPTION_CODE_DENSITY,
    XTENSA_OPTION_LOOP,
    XTENSA_OPTION_EXTENDED_L32R,
    XTENSA_OPTION_16_BIT_IMUL,
    XTENSA_OPTION_32_BIT_IMUL,
    XTENSA_OPTION_32_BIT_IMUL_HIGH,
    XTENSA_OPTION_32_BIT_IDIV,
    XTENSA_OPTION_MAC16,
    XTENSA_OPTION_MISC_OP_NSA,
    XTENSA_OPTION_MISC_OP_MINMAX,
    XTENSA_OPTION_MISC_OP_SEXT,
    XTENSA_OPTION_MISC_OP_CLAMPS,
    XTENSA_OPTION_COPROCESSOR,
    XTENSA_OPTION_BOOLEAN,
    XTENSA_OPTION_FP_COPROCESSOR,
    XTENSA_OPTION_DFP_COPROCESSOR,
    XTENSA_OPTION_DFPU_SINGLE_ONLY,
    XTENSA_OPTION_MP_SYNCHRO,
    XTENSA_OPTION_CONDITIONAL_STORE,
    XTENSA_OPTION_ATOMCTL,
    XTENSA_OPTION_DEPBITS,

    /* Interrupts and exceptions */
    XTENSA_OPTION_EXCEPTION,
    XTENSA_OPTION_RELOCATABLE_VECTOR,
    XTENSA_OPTION_UNALIGNED_EXCEPTION,
    XTENSA_OPTION_INTERRUPT,
    XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
    XTENSA_OPTION_TIMER_INTERRUPT,

    /* Local memory */
    XTENSA_OPTION_ICACHE,
    XTENSA_OPTION_ICACHE_TEST,
    XTENSA_OPTION_ICACHE_INDEX_LOCK,
    XTENSA_OPTION_DCACHE,
    XTENSA_OPTION_DCACHE_TEST,
    XTENSA_OPTION_DCACHE_INDEX_LOCK,
    XTENSA_OPTION_IRAM,
    XTENSA_OPTION_IROM,
    XTENSA_OPTION_DRAM,
    XTENSA_OPTION_DROM,
    XTENSA_OPTION_XLMI,
    XTENSA_OPTION_HW_ALIGNMENT,
    XTENSA_OPTION_MEMORY_ECC_PARITY,
    XTENSA_OPTION_PREFETCH,

    /* Memory protection and translation */
    XTENSA_OPTION_REGION_PROTECTION,
    XTENSA_OPTION_REGION_TRANSLATION,
    XTENSA_OPTION_MPU,
    XTENSA_OPTION_MMU,
    XTENSA_OPTION_CACHEATTR,

    /* Other */
    XTENSA_OPTION_WINDOWED_REGISTER,
    XTENSA_OPTION_PROCESSOR_INTERFACE,
    XTENSA_OPTION_MISC_SR,
    XTENSA_OPTION_THREAD_POINTER,
    XTENSA_OPTION_PROCESSOR_ID,
    XTENSA_OPTION_DEBUG,
    XTENSA_OPTION_TRACE_PORT,
    XTENSA_OPTION_EXTERN_REGS,
};

// User Registers (cpu->uregs).
enum {
    EXPSTATE = 230,
    THREADPTR = 231,
    FCR = 232,
    FSR = 233,
};

// Special registers (cpu->sregs).
enum {
    LBEG = 0,
    LEND = 1,
    LCOUNT = 2,
    SAR = 3,
    BR = 4,
    LITBASE = 5,
    SCOMPARE1 = 12,
    ACCLO = 16,
    ACCHI = 17,
    MR = 32,
    PREFCTL = 40,
    WINDOW_BASE = 72,
    WINDOW_START = 73,
    PTEVADDR = 83,
    MMID = 89,
    RASID = 90,
    MPUENB = 90,
    ITLBCFG = 91,
    DTLBCFG = 92,
    MPUCFG = 92,
    ERACCESS = 95,
    IBREAKENABLE = 96,
    MEMCTL = 97,
    CACHEATTR = 98,
    CACHEADRDIS = 98,
    ATOMCTL = 99,
    DDR = 104,
    MEPC = 106,
    MEPS = 107,
    MESAVE = 108,
    MESR = 109,
    MECR = 110,
    MEVADDR = 111,
    IBREAKA = 128,
    DBREAKA = 144,
    DBREAKC = 160,
    CONFIGID0 = 176,
    EPC1 = 177,
    DEPC = 192,
    EPS2 = 194,
    CONFIGID1 = 208,
    EXCSAVE1 = 209,
    CPENABLE = 224,
    INTSET = 226,
    INTCLEAR = 227,
    INTENABLE = 228,
    PS = 230,
    VECBASE = 231,
    EXCCAUSE = 232,
    DEBUGCAUSE = 233,
    CCOUNT = 234,
    PRID = 235,
    ICOUNT = 236,
    ICOUNTLEVEL = 237,
    EXCVADDR = 238,
    CCOMPARE = 240,
    MISC = 244,
};

#define PS_INTLEVEL 0xf
#define PS_INTLEVEL_SHIFT 0

#define PS_EXCM 0x10
#define PS_UM 0x20

#define PS_RING 0xc0
#define PS_RING_SHIFT 6

#define PS_OWB 0xf00
#define PS_OWB_SHIFT 8
#define PS_OWB_LEN 4

#define PS_CALLINC 0x30000
#define PS_CALLINC_SHIFT 16
#define PS_CALLINC_LEN 2

#define PS_WOE 0x40000

#define DEBUGCAUSE_IC 0x1
#define DEBUGCAUSE_IB 0x2
#define DEBUGCAUSE_DB 0x4
#define DEBUGCAUSE_BI 0x8
#define DEBUGCAUSE_BN 0x10
#define DEBUGCAUSE_DI 0x20
#define DEBUGCAUSE_DBNUM 0xf00
#define DEBUGCAUSE_DBNUM_SHIFT 8

#define DBREAKC_SB 0x80000000
#define DBREAKC_LB 0x40000000
#define DBREAKC_SB_LB (DBREAKC_SB | DBREAKC_LB)
#define DBREAKC_MASK 0x3f

#define MEMCTL_INIT 0x00800000
#define MEMCTL_IUSEWAYS_SHIFT 18
#define MEMCTL_IUSEWAYS_LEN 5
#define MEMCTL_IUSEWAYS_MASK 0x007c0000
#define MEMCTL_DALLOCWAYS_SHIFT 13
#define MEMCTL_DALLOCWAYS_LEN 5
#define MEMCTL_DALLOCWAYS_MASK 0x0003e000
#define MEMCTL_DUSEWAYS_SHIFT 8
#define MEMCTL_DUSEWAYS_LEN 5
#define MEMCTL_DUSEWAYS_MASK 0x00001f00
#define MEMCTL_ISNP 0x4
#define MEMCTL_DSNP 0x2
#define MEMCTL_IL0EN 0x1

#define MAX_INSN_LENGTH 64
#define MAX_INSNBUF_LENGTH \
    ((MAX_INSN_LENGTH + sizeof(xtensa_insnbuf_word) - 1) / \
     sizeof(xtensa_insnbuf_word))
#define MAX_INSN_SLOTS 32
#define MAX_OPCODE_ARGS 16
#define MAX_NAREG 64
#define MAX_NINTERRUPT 32
#define MAX_NLEVEL 6
#define MAX_NNMI 1
#define MAX_NCCOMPARE 3
#define MAX_TLB_WAY_SIZE 8
#define MAX_NDBREAK 2
#define MAX_NMEMORY 4
#define MAX_MPU_FOREGROUND_SEGMENTS 32

#define REGION_PAGE_MASK 0xe0000000

#define PAGE_CACHE_MASK    0x700
#define PAGE_CACHE_SHIFT   8
#define PAGE_CACHE_INVALID 0x000
#define PAGE_CACHE_BYPASS  0x100
#define PAGE_CACHE_WT      0x200
#define PAGE_CACHE_WB      0x400
#define PAGE_CACHE_ISOLATE 0x600

enum {
    /* Static vectors */
    EXC_RESET0,
    EXC_RESET1,
    EXC_MEMORY_ERROR,

    /* Dynamic vectors */
    EXC_WINDOW_OVERFLOW4,
    EXC_WINDOW_UNDERFLOW4,
    EXC_WINDOW_OVERFLOW8,
    EXC_WINDOW_UNDERFLOW8,
    EXC_WINDOW_OVERFLOW12,
    EXC_WINDOW_UNDERFLOW12,
    EXC_IRQ,
    EXC_KERNEL,
    EXC_USER,
    EXC_DOUBLE,
    EXC_DEBUG,
    EXC_MAX
};

enum {
    ILLEGAL_INSTRUCTION_CAUSE = 0,
    SYSCALL_CAUSE,
    INSTRUCTION_FETCH_ERROR_CAUSE,
    LOAD_STORE_ERROR_CAUSE,
    LEVEL1_INTERRUPT_CAUSE,
    ALLOCA_CAUSE,
    INTEGER_DIVIDE_BY_ZERO_CAUSE,
    PC_VALUE_ERROR_CAUSE,
    PRIVILEGED_CAUSE,
    LOAD_STORE_ALIGNMENT_CAUSE,
    EXTERNAL_REG_PRIVILEGE_CAUSE,
    EXCLUSIVE_ERROR_CAUSE,
    INSTR_PIF_DATA_ERROR_CAUSE,
    LOAD_STORE_PIF_DATA_ERROR_CAUSE,
    INSTR_PIF_ADDR_ERROR_CAUSE,
    LOAD_STORE_PIF_ADDR_ERROR_CAUSE,
    INST_TLB_MISS_CAUSE,
    INST_TLB_MULTI_HIT_CAUSE,
    INST_FETCH_PRIVILEGE_CAUSE,
    INST_FETCH_PROHIBITED_CAUSE = 20,
    LOAD_STORE_TLB_MISS_CAUSE = 24,
    LOAD_STORE_TLB_MULTI_HIT_CAUSE,
    LOAD_STORE_PRIVILEGE_CAUSE,
    LOAD_PROHIBITED_CAUSE = 28,
    STORE_PROHIBITED_CAUSE,

    COPROCESSOR0_DISABLED = 32,
};

typedef enum {
    INTTYPE_LEVEL,
    INTTYPE_EDGE,
    INTTYPE_NMI,
    INTTYPE_SOFTWARE,
    INTTYPE_TIMER,
    INTTYPE_DEBUG,
    INTTYPE_WRITE_ERR,
    INTTYPE_PROFILING,
    INTTYPE_IDMA_DONE,
    INTTYPE_IDMA_ERR,
    INTTYPE_GS_ERR,
    INTTYPE_MAX
} interrupt_type;

typedef struct xtensa_tlb_entry {
    uint32_t vaddr;
    uint32_t paddr;
    uint8_t asid;
    uint8_t attr;
    bool variable;
} xtensa_tlb_entry;

typedef struct xtensa_tlb {
    unsigned nways;
    const unsigned way_size[10];
    bool varway56;
    unsigned nrefillentries;
} xtensa_tlb;

typedef struct xtensa_mpu_entry {
    uint32_t vaddr;
    uint32_t attr;
} xtensa_mpu_entry;

typedef struct XtensaMemory {
    unsigned num;
    struct XtensaMemoryRegion {
        uint32_t addr;
        uint32_t size;
    } location[MAX_NMEMORY];
} XtensaMemory;

typedef struct opcode_arg {
    uint32_t imm;
    uint32_t raw_imm;
    int in;
    int out;
    uint32_t num_bits;
} OpcodeArg;

typedef struct DisasContext DisasContext;
typedef void (*XtensaOpcodeOp)(DisasContext *dc, const OpcodeArg arg[],
                               const uint32_t par[]);
typedef uint32_t (*XtensaOpcodeUintTest)(DisasContext *dc,
                                         const OpcodeArg arg[],
                                         const uint32_t par[]);

enum {
    XTENSA_OP_ILL = 0x1,
    XTENSA_OP_PRIVILEGED = 0x2,
    XTENSA_OP_SYSCALL = 0x4,
    XTENSA_OP_DEBUG_BREAK = 0x8,

    XTENSA_OP_OVERFLOW = 0x10,
    XTENSA_OP_UNDERFLOW = 0x20,
    XTENSA_OP_ALLOCA = 0x40,
    XTENSA_OP_COPROCESSOR = 0x80,

    XTENSA_OP_DIVIDE_BY_ZERO = 0x100,

    /* Postprocessing flags */
    XTENSA_OP_CHECK_INTERRUPTS = 0x200,
    XTENSA_OP_EXIT_TB_M1 = 0x400,
    XTENSA_OP_EXIT_TB_0 = 0x800,
    XTENSA_OP_SYNC_REGISTER_WINDOW = 0x1000,

    XTENSA_OP_POSTPROCESS =
        XTENSA_OP_CHECK_INTERRUPTS |
        XTENSA_OP_EXIT_TB_M1 |
        XTENSA_OP_EXIT_TB_0 |
        XTENSA_OP_SYNC_REGISTER_WINDOW,

    XTENSA_OP_NAME_ARRAY = 0x8000,

    XTENSA_OP_CONTROL_FLOW = 0x10000,
    XTENSA_OP_STORE = 0x20000,
    XTENSA_OP_LOAD = 0x40000,
    XTENSA_OP_LOAD_STORE =
        XTENSA_OP_LOAD | XTENSA_OP_STORE,
};

typedef struct XtensaOpcodeOps {
    const void *name;
    XtensaOpcodeOp translate;
    XtensaOpcodeUintTest test_exceptions;
    XtensaOpcodeUintTest test_overflow;
    const uint32_t *par;
    uint32_t op_flags;
    uint32_t coprocessor;
} XtensaOpcodeOps;

typedef struct XtensaOpcodeTranslators {
    unsigned num_opcodes;
    const XtensaOpcodeOps *opcode;
} XtensaOpcodeTranslators;

extern const XtensaOpcodeTranslators xtensa_core_opcodes;
extern const XtensaOpcodeTranslators xtensa_fpu2000_opcodes;
extern const XtensaOpcodeTranslators xtensa_fpu_opcodes;

struct XtensaConfig {
    const char *name;
    uint64_t options;
    unsigned nareg;
    int excm_level;
    int ndepc;
    unsigned inst_fetch_width;
    unsigned max_insn_size;
    uint32_t vecbase;
    uint32_t exception_vector[EXC_MAX];
    unsigned ninterrupt;
    unsigned nlevel;
    unsigned nmi_level;
    uint32_t interrupt_vector[MAX_NLEVEL + MAX_NNMI + 1];
    uint32_t level_mask[MAX_NLEVEL + MAX_NNMI + 1];
    uint32_t inttype_mask[INTTYPE_MAX];
    struct {
        uint32_t level;
        interrupt_type inttype;
    } interrupt[MAX_NINTERRUPT];
    unsigned nccompare;
    uint32_t timerint[MAX_NCCOMPARE];
    unsigned nextint;
    unsigned extint[MAX_NINTERRUPT];

    unsigned debug_level;
    unsigned nibreak;
    unsigned ndbreak;

    unsigned icache_ways;
    unsigned dcache_ways;
    unsigned dcache_line_bytes;
    uint32_t memctl_mask;

    XtensaMemory instrom;
    XtensaMemory instram;
    XtensaMemory datarom;
    XtensaMemory dataram;
    XtensaMemory sysrom;
    XtensaMemory sysram;

    unsigned hw_version;
    uint32_t configid[2];

    void *isa_internal;
    xtensa_isa isa;
    XtensaOpcodeOps **opcode_ops;
    const XtensaOpcodeTranslators **opcode_translators;
    xtensa_regfile a_regfile;
    int **regfile;

    uint32_t clock_freq_khz;

    xtensa_tlb itlb;
    xtensa_tlb dtlb;

    uint32_t mpu_align;
    unsigned n_mpu_fg_segments;
    unsigned n_mpu_bg_segments;
    const xtensa_mpu_entry *mpu_bg;

    bool use_first_nan;
};
typedef struct XtensaConfig XtensaConfig;

typedef struct XtensaConfigList {
    const XtensaConfig *config;
    struct XtensaConfigList *next;
} XtensaConfigList;

#ifdef HOST_WORDS_BIGENDIAN
enum {
    FP_F32_HIGH,
    FP_F32_LOW,
};
#else
enum {
    FP_F32_LOW,
    FP_F32_HIGH,
};
#endif

#include "cpu-defs.h"

typedef struct CPUState {
    const XtensaConfig *config;
    uint32_t regs[16];
    uint32_t pc;
    uint32_t sar;
    uint32_t sregs[256];
    uint32_t uregs[256];
    uint32_t phys_regs[MAX_NAREG];
    union {
        float32 f32[2];
        float64 f64;
    } fregs[16];
    float_status fp_status;
    uint32_t windowbase_next;
    uint32_t exclusive_addr;
    uint32_t exclusive_val;

    xtensa_tlb_entry itlb[7][MAX_TLB_WAY_SIZE];
    xtensa_tlb_entry dtlb[10][MAX_TLB_WAY_SIZE];
    xtensa_mpu_entry mpu_fg[MAX_MPU_FOREGROUND_SEGMENTS];
    unsigned autorefill_idx;
    int pending_irq_level; /* level of last raised IRQ */
    uint64_t time_base;
    uint64_t ccount_time;
    uint32_t ccount_base;

    int exception_taken;
    int yield_needed;

    /* Watchpoints for DBREAK registers */
    bool singlestep_enabled;
    pthread_mutex_t io_lock;
    CPU_COMMON
} CPUState;


void xtensa_cpu_set_irq_pending_bit(CPUState *cpu, uint32_t irq, uint32_t value);

void xtensa_collect_sr_names(const XtensaConfig *config);
void xtensa_translate_init(void);
int *xtensa_get_regfile_by_name(const char *name, int entries, int bits);
void xtensa_sync_window_from_phys(CPUState *env);
void xtensa_sync_phys_from_window(CPUState *env);
void xtensa_rotate_window(CPUState *env, uint32_t delta);
void xtensa_restore_owb(CPUState *env);
void debug_exception_env(CPUState *new_env, uint32_t cause);

#define XTENSA_OPTION_BIT(opt) (((uint64_t)1) << (opt))
#define XTENSA_OPTION_ALL (~(uint64_t)0)

static inline bool xtensa_option_bits_enabled(const XtensaConfig *config,
        uint64_t opt)
{
    return (config->options & opt) != 0;
}

static inline bool xtensa_option_enabled(const XtensaConfig *config, int opt)
{
    return xtensa_option_bits_enabled(config, XTENSA_OPTION_BIT(opt));
}

static inline int xtensa_get_cintlevel(const CPUState *env)
{
    int level = (env->sregs[PS] & PS_INTLEVEL) >> PS_INTLEVEL_SHIFT;
    if ((env->sregs[PS] & PS_EXCM) && env->config->excm_level > level) {
        level = env->config->excm_level;
    }
    return level;
}

static inline int xtensa_get_ring(const CPUState *env)
{
    if (xtensa_option_bits_enabled(env->config,
                                   XTENSA_OPTION_BIT(XTENSA_OPTION_MMU) |
                                   XTENSA_OPTION_BIT(XTENSA_OPTION_MPU))) {
        return (env->sregs[PS] & PS_RING) >> PS_RING_SHIFT;
    } else {
        return 0;
    }
}

static inline int xtensa_get_cring(const CPUState *env)
{
    if (xtensa_option_bits_enabled(env->config,
                                   XTENSA_OPTION_BIT(XTENSA_OPTION_MMU) |
                                   XTENSA_OPTION_BIT(XTENSA_OPTION_MPU)) &&
        (env->sregs[PS] & PS_EXCM) == 0) {
        return (env->sregs[PS] & PS_RING) >> PS_RING_SHIFT;
    } else {
        return 0;
    }
}

int get_physical_address(CPUState *env, bool update_tlb,
        uint32_t vaddr, int is_write, int mmu_idx,
        uint32_t *paddr, uint32_t *page_size, int *access);
void reset_mmu(CPUState *env);

static inline uint32_t xtensa_replicate_windowstart(CPUState *env)
{
    return env->sregs[WINDOW_START] |
        (env->sregs[WINDOW_START] << env->config->nareg / 4);
}

/* MMU modes definitions */
#define MMU_USER_IDX 3

static inline int cpu_mmu_index(CPUState *env)
{
    return xtensa_get_cring(env);
}

#define XTENSA_TBFLAG_RING_MASK 0x3
#define XTENSA_TBFLAG_EXCM 0x4
#define XTENSA_TBFLAG_LITBASE 0x8
#define XTENSA_TBFLAG_DEBUG 0x10
#define XTENSA_TBFLAG_ICOUNT 0x20
#define XTENSA_TBFLAG_CPENABLE_MASK 0x3fc0
#define XTENSA_TBFLAG_CPENABLE_SHIFT 6
#define XTENSA_TBFLAG_EXCEPTION 0x4000
#define XTENSA_TBFLAG_WINDOW_MASK 0x18000
#define XTENSA_TBFLAG_WINDOW_SHIFT 15
#define XTENSA_TBFLAG_YIELD 0x20000
#define XTENSA_TBFLAG_CWOE 0x40000
#define XTENSA_TBFLAG_CALLINC_MASK 0x180000
#define XTENSA_TBFLAG_CALLINC_SHIFT 19

#define XTENSA_CSBASE_LEND_MASK 0x0000ffff
#define XTENSA_CSBASE_LEND_SHIFT 0
#define XTENSA_CSBASE_LBEG_OFF_MASK 0x00ff0000
#define XTENSA_CSBASE_LBEG_OFF_SHIFT 16

#include "cpu-all.h"

static inline void cpu_get_tb_cpu_state(CPUState *env, target_ulong *pc,
        target_ulong *cs_base, int *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = 0;
    *flags |= xtensa_get_ring(env);
    if (env->sregs[PS] & PS_EXCM) {
        *flags |= XTENSA_TBFLAG_EXCM;
    } else if (xtensa_option_enabled(env->config, XTENSA_OPTION_LOOP)) {
        target_ulong lend_dist =
            env->sregs[LEND] - (env->pc & -(1u << TARGET_PAGE_BITS));

        /*
         * 0 in the csbase_lend field means that there may not be a loopback
         * for any instruction that starts inside this page. Any other value
         * means that an instruction that ends at this offset from the page
         * start may loop back and will need loopback code to be generated.
         *
         * lend_dist is 0 when LEND points to the start of the page, but
         * no instruction that starts inside this page may end at offset 0,
         * so it's still correct.
         *
         * When an instruction ends at a page boundary it may only start in
         * the previous page. lend_dist will be encoded as TARGET_PAGE_SIZE
         * for the TB that contains this instruction.
         */
        if (lend_dist < (1u << TARGET_PAGE_BITS) + env->config->max_insn_size) {
            target_ulong lbeg_off = env->sregs[LEND] - env->sregs[LBEG];

            *cs_base = lend_dist;
            if (lbeg_off < 256) {
                *cs_base |= lbeg_off << XTENSA_CSBASE_LBEG_OFF_SHIFT;
            }
        }
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_EXTENDED_L32R) &&
            (env->sregs[LITBASE] & 1)) {
        *flags |= XTENSA_TBFLAG_LITBASE;
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_DEBUG)) {
        if (xtensa_get_cintlevel(env) < env->config->debug_level) {
            *flags |= XTENSA_TBFLAG_DEBUG;
        }
        if (xtensa_get_cintlevel(env) < env->sregs[ICOUNTLEVEL]) {
            *flags |= XTENSA_TBFLAG_ICOUNT;
        }
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_COPROCESSOR)) {
        *flags |= env->sregs[CPENABLE] << XTENSA_TBFLAG_CPENABLE_SHIFT;
    }
    if (env->singlestep_enabled && env->exception_taken) {
        *flags |= XTENSA_TBFLAG_EXCEPTION;
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_WINDOWED_REGISTER) &&
        (env->sregs[PS] & (PS_WOE | PS_EXCM)) == PS_WOE) {
        uint32_t windowstart = xtensa_replicate_windowstart(env) >>
            (env->sregs[WINDOW_BASE] + 1);
        uint32_t w = ctz32(windowstart | 0x8);

        *flags |= (w << XTENSA_TBFLAG_WINDOW_SHIFT) | XTENSA_TBFLAG_CWOE;
        *flags |= extract32(env->sregs[PS], PS_CALLINC_SHIFT,
                            PS_CALLINC_LEN) << XTENSA_TBFLAG_CALLINC_SHIFT;
    } else {
        *flags |= 3 << XTENSA_TBFLAG_WINDOW_SHIFT;
    }
    if (env->yield_needed) {
        *flags |= XTENSA_TBFLAG_YIELD;
    }
}

#include "tcg-op.h"
struct DisasContext {
    DisasContextBase base;
    const XtensaConfig *config;
    uint32_t pc;
    int cring;
    int ring;
    uint32_t lbeg_off;
    uint32_t lend;

    bool sar_5bit;
    bool sar_m32_5bit;
    bool sar_m32_allocated;
    TCGv_i32 sar_m32;

    unsigned window;
    unsigned callinc;
    bool cwoe;

    bool debug;
    bool icount;
    TCGv_i32 next_icount;

    unsigned cpenable;

    uint32_t op_flags;
    xtensa_insnbuf_word insnbuf[MAX_INSNBUF_LENGTH];
    xtensa_insnbuf_word slotbuf[MAX_INSNBUF_LENGTH];
};

#include "exec-all.h"
static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
}

static inline bool cpu_has_work(CPUState *cs)
{
    // TODO: Implement properly.
    return true;
}

// For */xtensa-modules.c.inc from system's alsa/global.h (LGPL)
#ifndef ATTRIBUTE_UNUSED
/** do not print warning (gcc) when function parameter is not used */
#define ATTRIBUTE_UNUSED __attribute__ ((__unused__))
#endif

XtensaConfig* xtensa_finalize_config(const char *);

#define DISAS_NORETURN 4
#define DISAS_TOO_MANY 5

extern XtensaConfig apollolake;
extern XtensaConfig baytrail;
extern XtensaConfig cannonlake;
extern XtensaConfig dc233c;
extern XtensaConfig de212;
extern XtensaConfig de233_fpu;
extern XtensaConfig dsp3400;
extern XtensaConfig haswell;
extern XtensaConfig icelake;
extern XtensaConfig imx8;
extern XtensaConfig imx8m;
extern XtensaConfig sample_controller;
extern XtensaConfig test_kc705_be;
extern XtensaConfig test_mmuhifi_c3;
extern XtensaConfig tigerlake;

#endif
