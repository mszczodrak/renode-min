/*
 * internal execution defines for qemu
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _EXEC_ALL_H_
#define _EXEC_ALL_H_

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include "compiler.h"
#include "cpu.h"

extern CPUState *env;

/* Page tracking code uses ram addresses in system mode, and virtual
   addresses in userspace mode.  Define tb_page_addr_t to be an appropriate
   type.  */
typedef ram_addr_t tb_page_addr_t;

/* is_jmp field values */
#define DISAS_NEXT    0 /* next instruction can be analyzed */
#define DISAS_JUMP    1 /* only pc was modified dynamically */
#define DISAS_UPDATE  2 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP 3 /* only pc was modified statically */

#define EXIT_TB_NO_JUMP 0
#define EXIT_TB_JUMP    1
#define EXIT_TB_FORCE   2

struct TranslationBlock;
typedef struct TranslationBlock TranslationBlock;

void gen_exit_tb(TranslationBlock *, int);
void gen_exit_tb_no_chaining(TranslationBlock *);
CPUBreakpoint *process_breakpoints(CPUState *env, target_ulong pc);
int gen_intermediate_code(CPUState *env, DisasContextBase *base);
int gen_breakpoint(DisasContextBase *base, CPUBreakpoint *bp);
uint32_t gen_intermediate_code_epilogue(CPUState *env, DisasContextBase *base);
void do_interrupt(CPUState *env);
void setup_disas_context(DisasContextBase *dc, CPUState *env);
void restore_state_to_opc(CPUState *env, struct TranslationBlock *tb, target_ulong *data);

void cpu_gen_code(CPUState *env, struct TranslationBlock *tb, int *gen_code_size_ptr, int *search_size_ptr);
int cpu_restore_state_from_tb(CPUState *env, struct TranslationBlock *tb, uintptr_t searched_pc);
void cpu_restore_state(CPUState *env, void *retaddr);
int cpu_restore_state_and_restore_instructions_count(CPUState *env, struct TranslationBlock *tb, uintptr_t searched_pc);
TranslationBlock *tb_gen_code(CPUState *env, target_ulong pc, target_ulong cs_base, int flags, uint16_t cflags);
TranslationBlock *try_find_tb_at_pc(CPUState *env, target_ulong pc);
void cpu_exec_init(CPUState *env);
void cpu_exec_init_all();
void TLIB_NORETURN cpu_loop_exit(CPUState *env1);
void TLIB_NORETURN cpu_loop_exit_restore(CPUState *env1, uintptr_t pc, uint32_t call_hook);
void tb_invalidate_phys_page_range(tb_page_addr_t start, tb_page_addr_t end, int is_cpu_write_access);
void tlb_flush_page(CPUState *env, target_ulong addr);
void tlb_flush(CPUState *env, int flush_global);
void tlb_set_page(CPUState *env, target_ulong vaddr, target_phys_addr_t paddr, int prot, int mmu_idx, target_ulong size);
void interrupt_current_translation_block(CPUState *env, int exception_type);
int get_external_mmu_phys_addr(CPUState *env, uint32_t address, int access_type,
                                                              target_phys_addr_t *phys_ptr, int *prot, int no_page_fault);

#define CODE_GEN_ALIGN           16 /* must be >= of the size of a icache line */

#define CODE_GEN_PHYS_HASH_BITS  15
#define CODE_GEN_PHYS_HASH_SIZE  (1 << CODE_GEN_PHYS_HASH_BITS)

#define MIN_CODE_GEN_BUFFER_SIZE (1024 * 1024)

/* estimated block size for TB allocation */
/* XXX: use a per code average code fragment size and modulate it
   according to the host CPU */
#define CODE_GEN_AVG_BLOCK_SIZE  128

extern uint32_t maximum_block_size;

struct TranslationBlock {
    target_ulong pc;      /* simulated PC corresponding to this block (EIP + CS base) */
    target_ulong cs_base; /* CS base for this block */
    uint64_t flags;       /* flags defining in which context the code was generated */
    uint32_t disas_flags;
    bool dirty_flag;      /* invalidation after write to an address from this block */
    uint16_t size;        /* size of target code for this block (1 <=
                             size <= TARGET_PAGE_SIZE) */
    uint16_t cflags;      /* compile flags */
#define CF_COUNT_MASK 0x7fff

    uint8_t *tc_ptr;      /* pointer to the translated code */
    uint8_t *tc_search;   /* pointer to search data */
    /* next matching tb for physical address. */
    struct TranslationBlock *phys_hash_next;
    /* first and second physical page containing code. The lower bit
       of the pointer tells the index in page_next[] */
    struct TranslationBlock *page_next[2];
    tb_page_addr_t page_addr[2];

    /* the following data are used to directly call another TB from
       the code of this one. */
    uint16_t tb_next_offset[2]; /* offset of original jump target */
    uint16_t tb_jmp_offset[2];  /* offset of jump instruction */
    /* list of TBs jumping to this one. This is a circular list using
       the two least significant bits of the pointers to tell what is
       the next pointer: 0 = jmp_next[0], 1 = jmp_next[1], 2 =
       jmp_first */
    struct TranslationBlock *jmp_next[2];
    struct TranslationBlock *jmp_first;
    // the type of this field needs to match the TCG-generated access in `gen_update_instructions_count` in translate-all.c
    uint32_t icount;
    bool was_cut;
    // this field is used to keep track of the previous value of size, i.e., it shows the size of translation block without the last instruction; used by a blockend hook
    uint16_t prev_size;
    // signals that the `icount` of this tb has been added to global instructions counters
    // in case of exiting this tb before the end (e.g., in case of an exception, watchpoint etc.) the value of counters must be rebuilt
    // the type of this field needs to match the TCG-generated access in `gen_update_instructions_count` in translate-all.c
    uint32_t instructions_count_dirty;
#if DEBUG
    uint32_t lock_active;
    char *lock_file;
    int lock_line;
#endif
};

static inline unsigned int tb_jmp_cache_hash_page(target_ulong pc)
{
    target_ulong tmp;
    tmp = pc ^ (pc >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS));
    return (tmp >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS)) & TB_JMP_PAGE_MASK;
}

static inline unsigned int tb_jmp_cache_hash_func(target_ulong pc)
{
    target_ulong tmp;
    tmp = pc ^ (pc >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS));
    return (((tmp >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS)) & TB_JMP_PAGE_MASK) | (tmp & TB_JMP_ADDR_MASK));
}

static inline unsigned int tb_phys_hash_func(tb_page_addr_t pc)
{
    return (pc >> 2) & (CODE_GEN_PHYS_HASH_SIZE - 1);
}

void tb_free(TranslationBlock *tb);
void tb_flush(CPUState *env);
void tb_link_page(TranslationBlock *tb, tb_page_addr_t phys_pc, tb_page_addr_t phys_page2);
void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr);

extern TranslationBlock *tb_phys_hash[CODE_GEN_PHYS_HASH_SIZE];

#if defined(__i386__) || defined(__x86_64__)
static inline void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr)
{
    /* patch the branch destination */
    *(uint32_t *)jmp_addr = addr - (jmp_addr + 4);
    /* no need to flush icache explicitly */
}
#elif defined(__arm__) || defined(__aarch64__)
static inline void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr)
{
#if !defined(__GNUC__)
    register unsigned long _beg __asm ("a1");
    register unsigned long _end __asm ("a2");
    register unsigned long _flg __asm ("a3");
#endif

    /* we could use a ldr pc, [pc, #-4] kind of branch and avoid the flush */
    *(uint32_t *)jmp_addr =
        (*(uint32_t *)jmp_addr & ~0xffffff) | (((addr - (jmp_addr + 8)) >> 2) & 0xffffff);

#if defined(__GNUC__)
    __builtin___clear_cache((char *)jmp_addr, (char *)jmp_addr + 4);
#else
    /* flush icache */
    _beg = jmp_addr;
    _end = jmp_addr + 4;
    _flg = 0;
    __asm __volatile__ ("swi 0x9f0002" : : "r" (_beg), "r" (_end), "r" (_flg));
#endif
}
#else
#error tb_set_jmp_target1 is missing
#endif

static inline void tb_set_jmp_target(TranslationBlock *tb, int n, uintptr_t addr)
{
    uintptr_t offset;

    offset = tb->tb_jmp_offset[n];
    tb_set_jmp_target1((uintptr_t)(tb->tc_ptr + offset), addr);
}

static inline void tb_add_jump(TranslationBlock *tb, int n, TranslationBlock *tb_next)
{
    /* NOTE: this test is only needed for thread safety */
    if (!tb->jmp_next[n]) {
        /* patch the native jump address */
        tb_set_jmp_target(tb, n, (uintptr_t)tb_next->tc_ptr);

        /* add in TB jmp circular list */
        tb->jmp_next[n] = tb_next->jmp_first;
        tb_next->jmp_first = (TranslationBlock *)((uintptr_t)(tb) | (n));
    }
}

TranslationBlock *tb_find_pc(uintptr_t pc_ptr);

extern int tb_invalidated_flag;


int tlb_fill(CPUState *env1, target_ulong addr, int is_write, int mmu_idx, void *retaddr, int no_page_fault, int access_width);

void mark_tbs_containing_pc_as_dirty(target_ulong addr, int broadcast);
void flush_dirty_addresses_list(void);
void append_dirty_address(uint64_t address);

#include "softmmu_defs.h"

#define ACCESS_TYPE (NB_MMU_MODES + 1)
#define MEMSUFFIX   _code
#define env         cpu

#define DATA_SIZE   1
#include "softmmu_header.h"

#define DATA_SIZE   2
#include "softmmu_header.h"

#define DATA_SIZE   4
#include "softmmu_header.h"

#define DATA_SIZE   8
#include "softmmu_header.h"

#undef ACCESS_TYPE
#undef MEMSUFFIX
#undef env

/* NOTE: this function can trigger an exception when used with `map_when_needed == true` */
/* NOTE2: the returned address is not exactly the physical address: it
   is the offset relative to phys_ram_base */
static inline tb_page_addr_t get_page_addr_code(CPUState *env1, target_ulong addr, bool map_when_needed)
{
    int mmu_idx, page_index;
    ram_addr_t pd;
    void *p;

    page_index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    mmu_idx = cpu_mmu_index(env1);
    if (unlikely(env1->tlb_table[mmu_idx][page_index].addr_code != (addr & TARGET_PAGE_MASK))) {
        if(map_when_needed)
        {
            ldub_code(addr);
        }
        else
        {
            return -1;
        }
    }
    pd = env1->tlb_table[mmu_idx][page_index].addr_code & ~TARGET_PAGE_MASK;
    if (pd > IO_MEM_ROM && !(pd & IO_MEM_ROMD)) {
        cpu_abort(env1, "Trying to execute code outside RAM or ROM at 0x" TARGET_FMT_lx "\n", addr);
    }
    p = (void *)((uintptr_t)addr + env1->tlb_table[mmu_idx][page_index].addend);
    return ram_addr_from_host(p);
}

typedef void (CPUDebugExcpHandler)(CPUState *env);

CPUDebugExcpHandler *cpu_set_debug_excp_handler(CPUDebugExcpHandler *handler);

/* cpu-exec.c */
PhysPageDesc *phys_page_find(target_phys_addr_t index);

void tb_invalidate_phys_page_range_inner(tb_page_addr_t start, tb_page_addr_t end, int is_cpu_write_access, int broadcast);

extern void unmap_page(target_phys_addr_t address);
void free_all_page_descriptors(void);
void code_gen_free(void);

void generate_opcode_count_increment(CPUState*, uint64_t);
uint64_t get_register_value(int reg_number);
void set_register_value(int reg_number, uint64_t val);
void tlib_announce_stack_change(target_ulong pc, int state);
void prepare_stack_change_announcement(target_ulong pc, int state);
void tlib_announce_context_change(target_ulong context_id);

#endif
