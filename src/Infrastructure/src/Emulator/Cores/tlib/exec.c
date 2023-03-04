/*
 *  virtual page mapping and translated block handling
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
#include "infrastructure.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "cpu.h"
#include "tcg.h"
#include "osdep.h"

#define SMC_BITMAP_USE_THRESHOLD 10

CPUState *env;
extern void* global_retaddr;

static TranslationBlock *tbs;
static int code_gen_max_blocks;
TranslationBlock *tb_phys_hash[CODE_GEN_PHYS_HASH_SIZE];
static int nb_tbs;
/* any access to the tbs or the page table must use this lock */

static uint8_t *code_gen_buffer;
static uintptr_t code_gen_buffer_size;
/* threshold to flush the translated code buffer */
static uintptr_t code_gen_buffer_max_size;
static uint8_t *code_gen_ptr;

dirty_ram_t dirty_ram = {0, 0};

CPUState *cpu;

typedef struct PageDesc {
    /* list of TBs intersecting this ram page */
    TranslationBlock *first_tb;
    /* in order to optimize self modifying code, we count the number
       of lookups we do to a given page to use a bitmap */
    unsigned int code_write_count;
    uint8_t *code_bitmap;
} PageDesc;

/* In system mode we want L1_MAP to be based on ram offsets,
   while in user mode we want it to be based on virtual addresses.  */
#if HOST_LONG_BITS < TARGET_PHYS_ADDR_SPACE_BITS
# define L1_MAP_ADDR_SPACE_BITS HOST_LONG_BITS
#else
# define L1_MAP_ADDR_SPACE_BITS TARGET_PHYS_ADDR_SPACE_BITS
#endif

/* Size of the L2 (and L3, etc) page tables.  */
#define L2_BITS                 10
#define L2_SIZE                 (1 << L2_BITS)

/* The bits remaining after N lower levels of page tables.  */
#define P_L1_BITS_REM \
    ((TARGET_PHYS_ADDR_SPACE_BITS - TARGET_PAGE_BITS) % L2_BITS)
#define V_L1_BITS_REM \
    ((L1_MAP_ADDR_SPACE_BITS - TARGET_PAGE_BITS) % L2_BITS)

/* Size of the L1 page table.  Avoid silly small sizes.  */
#if P_L1_BITS_REM < 4
#define P_L1_BITS  (P_L1_BITS_REM + L2_BITS)
#else
#define P_L1_BITS  P_L1_BITS_REM
#endif

#if V_L1_BITS_REM < 4
#define V_L1_BITS  (V_L1_BITS_REM + L2_BITS)
#else
#define V_L1_BITS  V_L1_BITS_REM
#endif

#define P_L1_SIZE  ((target_phys_addr_t)1 << P_L1_BITS)
#define V_L1_SIZE  ((target_ulong)1 << V_L1_BITS)

#define P_L1_SHIFT (TARGET_PHYS_ADDR_SPACE_BITS - TARGET_PAGE_BITS - P_L1_BITS)
#define V_L1_SHIFT (L1_MAP_ADDR_SPACE_BITS - TARGET_PAGE_BITS - V_L1_BITS)

uintptr_t tlib_real_host_page_size;
uintptr_t tlib_host_page_bits;
uintptr_t tlib_host_page_size;
uintptr_t tlib_host_page_mask;

/* This is a multi-level map on the virtual address space.
   The bottom level has pointers to PageDesc.  */
static void *l1_map[V_L1_SIZE];

/* This is a multi-level map on the physical address space.
   The bottom level has pointers to PhysPageDesc.  */
static void *l1_phys_map[P_L1_SIZE];


/* statistics */
static int tlb_flush_count;
static int tb_flush_count;
static int tb_phys_invalidate_count;

#ifdef _WIN32
static void map_exec(void *addr, long size)
{
    DWORD old_protect;
    int temp = VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &old_protect);
    temp++;

}
#else
static void map_exec(void *addr, long size)
{
    uintptr_t start, end, page_size;

    page_size = getpagesize();
    start = (uintptr_t)addr;
    start &= ~(page_size - 1);

    end = (uintptr_t)addr + size;
    end += page_size - 1;
    end &= ~(page_size - 1);

    mprotect((void *)start, end - start, PROT_READ | PROT_WRITE | PROT_EXEC);
}
#endif

static void page_init(void)
{
    /* NOTE: we can always suppose that tlib_host_page_size >=
       TARGET_PAGE_SIZE */
#ifdef _WIN32
    {
        SYSTEM_INFO system_info;

        GetSystemInfo(&system_info);
        tlib_real_host_page_size = system_info.dwPageSize;
    }
#else
    tlib_real_host_page_size = getpagesize();
#endif
    if (tlib_host_page_size == 0) {
        tlib_host_page_size = tlib_real_host_page_size;
    }
    if (tlib_host_page_size < TARGET_PAGE_SIZE) {
        tlib_host_page_size = TARGET_PAGE_SIZE;
    }
    tlib_host_page_bits = 0;
    while ((1 << tlib_host_page_bits) < tlib_host_page_size) {
        tlib_host_page_bits++;
    }
    tlib_host_page_mask = ~(tlib_host_page_size - 1);
}

typedef void (*visitor_function)(void *opaque, int page_number);

static void free_page_code_bitmap(void *opaque, int page_number)
{
    PageDesc *page;

    page = ((PageDesc *)opaque) + page_number;
    if (page->code_bitmap) {
        tlib_free(page->code_bitmap);
    }
}

static void free_all_page_descriptors_inner(void **lp, int level, visitor_function visitor)
{
    int i;

    if (!level) {
        // why the pointer below does not have to be of type
        // PageDesc/PhysPageDesc? because it does not change anything from the
        // free() point of view
        void *pd = *lp;
        if (pd) {
            for (i = 0; i < L2_SIZE; i++) {
                if (visitor) {
                    visitor(pd, i);
                }
            }
            tlib_free(pd);
        }
    } else {
        void **pp = *lp;
        if (!pp) {
            return;
        }
        for (i = 0; i < L2_SIZE; i++) {
            free_all_page_descriptors_inner(pp + i, level - 1, visitor);
        }
        tlib_free(pp);
    }
}

void free_all_page_descriptors()
{
    int i;

    for (i = 0; i < P_L1_SIZE; i++) {
        free_all_page_descriptors_inner(l1_phys_map + i, P_L1_SHIFT / L2_BITS - 1, NULL);
    }
    for (i = 0; i < V_L1_SIZE; i++) {
        free_all_page_descriptors_inner(l1_map + i, V_L1_SHIFT / L2_BITS - 1, free_page_code_bitmap);
    }
}

static PageDesc *page_find_alloc(tb_page_addr_t index, int alloc)
{
    PageDesc *pd;
    void **lp;
    int i;

    /* Level 1.  Always allocated.  */
    lp = l1_map + ((index >> V_L1_SHIFT) & (V_L1_SIZE - 1));

    /* Level 2..N-1.  */
    for (i = V_L1_SHIFT / L2_BITS - 1; i > 0; i--) {
        void **p = *lp;

        if (p == NULL) {
            if (!alloc) {
                return NULL;
            }
            p = tlib_mallocz(sizeof(void *) * L2_SIZE);
            *lp = p;
        }

        lp = p + ((index >> (i * L2_BITS)) & (L2_SIZE - 1));
    }

    pd = *lp;
    if (pd == NULL) {
        if (!alloc) {
            return NULL;
        }
        pd = tlib_mallocz(sizeof(PageDesc) * L2_SIZE);
        *lp = pd;
    }

    return pd + (index & (L2_SIZE - 1));
}

static inline PageDesc *page_find(tb_page_addr_t index)
{
    return page_find_alloc(index, 0);
}

static PhysPageDesc *phys_page_find_alloc(target_phys_addr_t index, int alloc)
{
    PhysPageDesc *pd;
    void **lp;
    int i;
    target_phys_addr_t aligned_index;

    /* Level 1.  Always allocated.  */
    lp = l1_phys_map + ((index >> P_L1_SHIFT) & (P_L1_SIZE - 1));

    /* Level 2..N-1.  */
    for (i = P_L1_SHIFT / L2_BITS - 1; i > 0; i--) {
        void **p = *lp;
        if (p == NULL) {
            if (!alloc) {
                return NULL;
            }
            *lp = p = tlib_mallocz(sizeof(void *) * L2_SIZE);
        }
        lp = p + ((index >> (i * L2_BITS)) & (L2_SIZE - 1));
    }

    pd = *lp;
    if (pd == NULL) {
        int i;

        if (!alloc) {
            return NULL;
        }

        *lp = pd = tlib_malloc(sizeof(PhysPageDesc) * L2_SIZE);
        aligned_index = index & ~(L2_SIZE - 1);

        for (i = 0; i < L2_SIZE; i++) {
            pd[i].phys_offset = IO_MEM_UNASSIGNED;
            pd[i].region_offset = (aligned_index + i) << TARGET_PAGE_BITS;
        }
    }

    return pd + (index & (L2_SIZE - 1));
}

inline PhysPageDesc *phys_page_find(target_phys_addr_t index)
{
    return phys_page_find_alloc(index, 0);
}

void unmap_page(target_phys_addr_t address)
{
    PhysPageDesc *pd;

    pd = phys_page_find(address >> TARGET_PAGE_BITS);
    if (pd == NULL) {
        return;
    }
    if (pd->phys_offset != IO_MEM_UNASSIGNED) {
        pd->region_offset = pd->phys_offset;
        pd->phys_offset = IO_MEM_UNASSIGNED;
    }
}

static void tlb_protect_code(ram_addr_t ram_addr);
static void tlb_unprotect_code_phys(CPUState *env, ram_addr_t ram_addr, target_ulong vaddr);
#define mmap_lock()   do { } while(0)
#define mmap_unlock() do { } while(0)

#define DEFAULT_CODE_GEN_BUFFER_SIZE (32 * 1024 * 1024)

static void code_gen_alloc()
{
    code_gen_buffer_size = translation_cache_size;
    if (code_gen_buffer_size < MIN_CODE_GEN_BUFFER_SIZE) {
        code_gen_buffer_size = MIN_CODE_GEN_BUFFER_SIZE;
    }
    /* The code gen buffer location may have constraints depending on
       the host cpu and OS */
#if defined(__linux__)
    {
        int flags;
        void *start = NULL;

        flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(__x86_64__)
        /* Cannot map more than that */
        if (code_gen_buffer_size > (800 * 1024 * 1024)) {
            code_gen_buffer_size = (800 * 1024 * 1024);
        }
#elif defined(__arm__)
        /* Map the buffer below 32M, so we can use direct calls and branches */
        flags |= MAP_FIXED;
        start = (void *)0x01000000UL;
        if (code_gen_buffer_size > 16 * 1024 * 1024) {
            code_gen_buffer_size = 16 * 1024 * 1024;
        }
#endif
        code_gen_buffer = mmap(start, code_gen_buffer_size, PROT_WRITE | PROT_READ | PROT_EXEC, flags, -1, 0);
        // let's give some feedback about what size was actually used
        tlib_on_translation_cache_size_change(code_gen_buffer_size);
        if (code_gen_buffer == MAP_FAILED) {
            tlib_abort("Could not allocate dynamic translator buffer\n");
        }
    }
#else
    code_gen_buffer = tlib_malloc(code_gen_buffer_size);
    map_exec(code_gen_buffer, code_gen_buffer_size);
#endif
    map_exec(tcg->code_gen_prologue, 1024);
    code_gen_buffer_max_size = code_gen_buffer_size - TCG_MAX_CODE_SIZE - TCG_MAX_SEARCH_SIZE;
    code_gen_max_blocks = code_gen_buffer_size / CODE_GEN_AVG_BLOCK_SIZE;
    tbs = tlib_malloc(code_gen_max_blocks * sizeof(TranslationBlock));
}

void code_gen_free(void)
{
#if defined(__linux__)
    int retval;
    retval = munmap(code_gen_buffer, code_gen_buffer_size);
    if (retval == -1) {
        tlib_abort("Could not free dynamic translator buffer\n");
    }
#else
    tlib_free(code_gen_buffer);
#endif
    tlib_free(tbs);
}

TCGv_ptr cpu_env;

/* Must be called before using the QEMU cpus.*/

void cpu_exec_init_all()
{
    tcg_context_init();
    code_gen_alloc();
    code_gen_ptr = code_gen_buffer;
    page_init();
    /* There's no guest base to take into account, so go ahead and
       initialize the prologue now.  */
    tcg_prologue_init();
    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
}

void cpu_exec_init(CPUState *env)
{
    cpu = env;
    QTAILQ_INIT(&cpu->breakpoints);
}

/* Allocate a new translation block. Flush the translation buffer if
   too many translation blocks or too much generated code. */
static TranslationBlock *tb_alloc(target_ulong pc)
{
    TranslationBlock *tb;

    if (nb_tbs >= code_gen_max_blocks || (code_gen_ptr - code_gen_buffer) >= code_gen_buffer_max_size) {
        return NULL;
    }
    tb = &tbs[nb_tbs++];
    tb->pc = pc;
    tb->cflags = 0;
    return tb;
}

void tb_free(TranslationBlock *tb)
{
    /* In practice this is mostly used for single use temporary TB
       Ignore the hard cases and just back up if this TB happens to
       be the last one generated.  */
    if (nb_tbs > 0 && tb == &tbs[nb_tbs - 1]) {
        code_gen_ptr = tb->tc_ptr;
        nb_tbs--;
    }
}

static inline void invalidate_page_bitmap(PageDesc *p)
{
    if (p->code_bitmap) {
        tlib_free(p->code_bitmap);
        p->code_bitmap = NULL;
    }
    p->code_write_count = 0;
}

/* Set to NULL all the 'first_tb' fields in all PageDescs. */

static void page_flush_tb_1 (int level, void **lp)
{
    int i;

    if (*lp == NULL) {
        return;
    }
    if (level == 0) {
        PageDesc *pd = *lp;
        for (i = 0; i < L2_SIZE; ++i) {
            pd[i].first_tb = NULL;
            invalidate_page_bitmap(pd + i);
        }
    } else {
        void **pp = *lp;
        for (i = 0; i < L2_SIZE; ++i) {
            page_flush_tb_1(level - 1, pp + i);
        }
    }
}

static void page_flush_tb(void)
{
    int i;
    for (i = 0; i < V_L1_SIZE; i++) {
        page_flush_tb_1(V_L1_SHIFT / L2_BITS - 1, l1_map + i);
    }
}

/* flush all the translation blocks */
/* XXX: tb_flush is currently not thread safe */
void tb_flush(CPUState *env1)
{
    if ((uintptr_t)(code_gen_ptr - code_gen_buffer) > code_gen_buffer_size) {
        cpu_abort(env1, "Internal error: code buffer overflow\n");
    }

    nb_tbs = 0;
    memset(cpu->tb_jmp_cache, 0, TB_JMP_CACHE_SIZE * sizeof (void *));
    memset(tb_phys_hash, 0, CODE_GEN_PHYS_HASH_SIZE * sizeof (void *));
    page_flush_tb();

    code_gen_ptr = code_gen_buffer;
    /* XXX: flush processor icache at this point if cache flush is
       expensive */
    tb_flush_count++;
}

/* invalidate one TB */
static inline void tb_remove(TranslationBlock **ptb, TranslationBlock *tb, int next_offset)
{
    TranslationBlock *tb1;
    for (;;) {
        tb1 = *ptb;
        if (tb1 == tb) {
            *ptb = *(TranslationBlock **)((char *)tb1 + next_offset);
            break;
        }
        ptb = (TranslationBlock **)((char *)tb1 + next_offset);
    }
}

static inline void tb_page_remove(TranslationBlock **ptb, TranslationBlock *tb)
{
    TranslationBlock *tb1;
    unsigned int n1;

    for (;;) {
        tb1 = *ptb;
        n1 = (uintptr_t)tb1 & 3;
        tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
        if (tb1 == tb) {
            *ptb = tb1->page_next[n1];
            break;
        }
        ptb = &tb1->page_next[n1];
    }
}

static inline void tb_jmp_remove(TranslationBlock *tb, int n)
{
    TranslationBlock *tb1, **ptb;
    unsigned int n1;

    ptb = &tb->jmp_next[n];
    tb1 = *ptb;
    if (tb1) {
        /* find tb(n) in circular list */
        for (;;) {
            tb1 = *ptb;
            n1 = (uintptr_t)tb1 & 3;
            tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
            if (n1 == n && tb1 == tb) {
                break;
            }
            if (n1 == EXIT_TB_FORCE) {
                ptb = &tb1->jmp_first;
            } else {
                ptb = &tb1->jmp_next[n1];
            }
        }
        /* now we can suppress tb(n) from the list */
        *ptb = tb->jmp_next[n];

        tb->jmp_next[n] = NULL;
    }
}

/* reset the jump entry 'n' of a TB so that it is not chained to
   another TB */
static inline void tb_reset_jump(TranslationBlock *tb, int n)
{
    tb_set_jmp_target(tb, n, (uintptr_t)(tb->tc_ptr + tb->tb_next_offset[n]));
}

void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr)
{
    PageDesc *p;
    unsigned int h, n1;
    tb_page_addr_t phys_pc;
    TranslationBlock *tb1, *tb2;

    /* remove the TB from the hash list */
    phys_pc = tb->page_addr[0] + (tb->pc & ~TARGET_PAGE_MASK);
    h = tb_phys_hash_func(phys_pc);
    tb_remove(&tb_phys_hash[h], tb, offsetof(TranslationBlock, phys_hash_next));

    /* remove the TB from the page list */
    if (tb->page_addr[0] != page_addr) {
        p = page_find(tb->page_addr[0] >> TARGET_PAGE_BITS);
        tb_page_remove(&p->first_tb, tb);
        invalidate_page_bitmap(p);
    }
    if (tb->page_addr[1] != -1 && tb->page_addr[1] != page_addr) {
        p = page_find(tb->page_addr[1] >> TARGET_PAGE_BITS);
        tb_page_remove(&p->first_tb, tb);
        invalidate_page_bitmap(p);
    }

    tb_invalidated_flag = 1;

    /* remove the TB from the hash list */
    h = tb_jmp_cache_hash_func(tb->pc);
    if (cpu->tb_jmp_cache[h] == tb) {
        cpu->tb_jmp_cache[h] = NULL;
    }

    /* suppress this TB from the two jump lists */
    tb_jmp_remove(tb, 0);
    tb_jmp_remove(tb, 1);

    /* suppress any remaining jumps to this TB */
    tb1 = tb->jmp_first;
    for (;;) {
        n1 = (uintptr_t)tb1 & 3;
        if (n1 == EXIT_TB_FORCE) {
            break;
        }
        tb1 = (TranslationBlock *)((uintptr_t)tb1 & ~3);
        tb2 = tb1->jmp_next[n1];
        tb_reset_jump(tb1, n1);
        tb1->jmp_next[n1] = NULL;
        tb1 = tb2;
    }
    tb->jmp_first = (TranslationBlock *)((uintptr_t)tb | EXIT_TB_FORCE); /* fail safe */
    tb_phys_invalidate_count++;
}

static inline void set_bits(uint8_t *tab, int start, int len)
{
    int end, mask, end1;

    end = start + len;
    tab += start >> 3;
    mask = 0xff << (start & 7);
    if ((start & ~7) == (end & ~7)) {
        if (start < end) {
            mask &= ~(0xff << (end & 7));
            *tab |= mask;
        }
    } else {
        *tab++ |= mask;
        start = (start + 8) & ~7;
        end1 = end & ~7;
        while (start < end1) {
            *tab++ = 0xff;
            start += 8;
        }
        if (start < end) {
            mask = ~(0xff << (end & 7));
            *tab |= mask;
        }
    }
}

static void build_page_bitmap(PageDesc *p)
{
    int n, tb_start, tb_end;
    TranslationBlock *tb;

    p->code_bitmap = tlib_mallocz(TARGET_PAGE_SIZE / 8);

    tb = p->first_tb;
    while (tb != NULL) {
        n = (uintptr_t)tb & 3;
        tb = (TranslationBlock *)((uintptr_t)tb & ~3);
        /* NOTE: this is subtle as a TB may span two physical pages */
        if (n == EXIT_TB_NO_JUMP) {
            /* NOTE: tb_end may be after the end of the page, but
               it is not a problem */
            tb_start = tb->pc & ~TARGET_PAGE_MASK;
            tb_end = tb_start + tb->size;
            if (tb_end > TARGET_PAGE_SIZE) {
                tb_end = TARGET_PAGE_SIZE;
            }
        } else {
            tb_start = 0;
            tb_end = ((tb->pc + tb->size) & ~TARGET_PAGE_MASK);
        }
        set_bits(p->code_bitmap, tb_start, tb_end - tb_start);
        tb = tb->page_next[n];
    }
}

TranslationBlock *tb_gen_code(CPUState *env, target_ulong pc, target_ulong cs_base, int flags, uint16_t cflags)
{
    TranslationBlock *tb;
    uint8_t *tc_ptr;
    tb_page_addr_t phys_pc, phys_page2;
    target_ulong virt_page2;
    int code_gen_size, search_size;

    phys_pc = get_page_addr_code(env, pc, true);
    tb = tb_alloc(pc);
    if (!tb) {
        /* flush must be done */
        tb_flush(env);
        /* cannot fail at this point */
        tb = tb_alloc(pc);
        /* Don't forget to invalidate previous TB info.  */
        tb_invalidated_flag = 1;
    }
    tc_ptr = code_gen_ptr;
    tb->tc_ptr = tc_ptr;
    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags;
    cpu_gen_code(env, tb, &code_gen_size, &search_size);
    code_gen_ptr = (void *)(((uintptr_t)code_gen_ptr + code_gen_size
        + search_size + CODE_GEN_ALIGN - 1) & ~(CODE_GEN_ALIGN - 1));

    /* check next page if needed */
    phys_page2 = -1;
    if (tb->size > 0) {
        // size will be 0 when tb contains a breakpoint instruction; in such case no other instructions are generated and there is no page2 at all
        virt_page2 = (pc + tb->size - 1) & TARGET_PAGE_MASK;
        if ((pc & TARGET_PAGE_MASK) != virt_page2) {
            phys_page2 = get_page_addr_code(env, virt_page2, true);
        }
    }
    tb_link_page(tb, phys_pc, phys_page2);
    return tb;
}

void helper_mark_tbs_as_dirty(CPUState *env, target_ulong pc, int broadcast)
{
    if (cpu->tb_cache_disabled) {
        return;
    }

    if (broadcast && cpu->tb_broadcast_dirty) {
        target_ulong masked_address = pc & TARGET_PAGE_MASK;
        append_dirty_address(masked_address);
    }

    int n;
    PageDesc *p;
    tb_page_addr_t phys_pc;
    TranslationBlock *tb, *tb_next;
    tb_page_addr_t tb_start, tb_end;

    // Try to find the page using the tlb contents
    phys_pc = get_page_addr_code(cpu, pc, false);
    if (phys_pc == -1 || !(p = page_find(phys_pc >> TARGET_PAGE_BITS))) {
        if (cpu->current_tb != 0) {
            // we are not on the same mem page, the mapping just does not exist
            return;
        }
        // Find the page using the platform specific mapping function
        // This is way slower, but it should be used only if the same page is being executed
        phys_pc = cpu_get_phys_page_debug(cpu, pc);
        if (phys_pc == -1 || !(p = page_find(phys_pc >> TARGET_PAGE_BITS))) {
            return;
        }
    }

    // Below code is a simplified version of the `tb_invalidate_phys_page_range_inner` search
    tb = p->first_tb;
    while (tb != NULL) {
        n = (uintptr_t)tb & 3;
        tb = (TranslationBlock *)((uintptr_t)tb & ~3);
        tb_next = tb->page_next[n];
        tb_start = tb->page_addr[0] + (tb->pc & ~TARGET_PAGE_MASK);
        tb_end = tb_start + tb->size;
        if (tb_start <= pc && tb_end > pc) {
            tb->dirty_flag = true;
        }
        tb = tb_next;
    }
}

/* invalidate all TBs which intersect with the target physical page
   starting in range [start;end[. NOTE: start and end must refer to
   the same physical page. 'is_cpu_write_access' should be true if called
   from a real cpu write access: the virtual CPU will exit the current
   TB if code is modified inside this TB. */
void tb_invalidate_phys_page_range_inner(tb_page_addr_t start, tb_page_addr_t end, int is_cpu_write_access, int broadcast)
{
    TranslationBlock *tb, *tb_next, *saved_tb;
    CPUState *env = cpu;
    tb_page_addr_t tb_start, tb_end;
    PageDesc *p;
    int n;
#ifdef TARGET_HAS_PRECISE_SMC
    int current_tb_not_found = is_cpu_write_access;
    TranslationBlock *current_tb = NULL;
    int current_tb_modified = 0;
    target_ulong current_pc = 0;
    target_ulong current_cs_base = 0;
    int current_flags = 0;
#endif /* TARGET_HAS_PRECISE_SMC */

    p = page_find(start >> TARGET_PAGE_BITS);
    if (!p) {
        return;
    }
    if (!p->code_bitmap && ++p->code_write_count >= SMC_BITMAP_USE_THRESHOLD && is_cpu_write_access) {
        /* build code bitmap */
        build_page_bitmap(p);
    }

    /* we remove all the TBs in the range [start, end[ */
    /* XXX: see if in some cases it could be faster to invalidate all the code */
    tb = p->first_tb;
    while (tb != NULL) {
        n = (uintptr_t)tb & 3;
        tb = (TranslationBlock *)((uintptr_t)tb & ~3);
        tb_next = tb->page_next[n];
        /* NOTE: this is subtle as a TB may span two physical pages */
        if (n == EXIT_TB_NO_JUMP) {
            /* NOTE: tb_end may be after the end of the page, but
               it is not a problem */
            tb_start = tb->page_addr[0] + (tb->pc & ~TARGET_PAGE_MASK);
            tb_end = tb_start + tb->size;
        } else {
            tb_start = tb->page_addr[1];
            tb_end = tb_start + ((tb->pc + tb->size) & ~TARGET_PAGE_MASK);
        }
        // condition in this form supports blocks where 'tb_start' == 'tb_end' (empty blocks with just a breakpoint)
        if ((tb_start >= start && tb_start < end) || (tb_end >= start && tb_end < end) || (tb_start <= start && tb_end >= end)) {
#ifdef TARGET_HAS_PRECISE_SMC
            if (current_tb_not_found) {
                current_tb_not_found = 0;
                current_tb = NULL;
                if (env->mem_io_pc) {
                    /* now we have a real cpu fault */
                    current_tb = tb_find_pc(env->mem_io_pc);
                }
            }
            if (current_tb == tb && (current_tb->cflags & CF_COUNT_MASK) != 1) {
                /* If we are modifying the current TB, we must stop
                   its execution. We could be more precise by checking
                   that the modification is after the current PC, but it
                   would require a specialized function to partially
                   restore the CPU state */

                current_tb_modified = 1;
                cpu_restore_state_from_tb(env, current_tb, env->mem_io_pc);
                cpu_get_tb_cpu_state(env, &current_pc, &current_cs_base, &current_flags);
            }
#endif /* TARGET_HAS_PRECISE_SMC */
            /* we need to do that to handle the case where a signal
               occurs while doing tb_phys_invalidate() */
            saved_tb = NULL;
            if (env) {
                saved_tb = env->current_tb;
                env->current_tb = NULL;
            }
            tb_phys_invalidate(tb, -1);
            if (env) {
                env->current_tb = saved_tb;
                if (env->interrupt_request && env->current_tb) {
                    cpu_interrupt(env, env->interrupt_request);
                }
            }
        }
        tb = tb_next;
    }
    /* if no code remaining, no need to continue to use slow writes */
    if (!p->first_tb) {
        invalidate_page_bitmap(p);
        if (is_cpu_write_access) {
            tlb_unprotect_code_phys(env, start, env->mem_io_vaddr);
        }
    }
    if (broadcast) {
        tlib_invalidate_tb_in_other_cpus(start, end);
    }
#ifdef TARGET_HAS_PRECISE_SMC
    if (current_tb_modified) {
        /* we generate a block containing just the instruction
           modifying the memory. It will ensure that it cannot modify
           itself */
        tb_gen_code(env, current_pc, current_cs_base, current_flags, 1);
        env->exception_index = -1;
        cpu_loop_exit(env);
    }
#endif
}

void tb_invalidate_phys_page_range(tb_page_addr_t start, tb_page_addr_t end, int is_cpu_write_access)
{
    tb_invalidate_phys_page_range_inner(start, end, is_cpu_write_access, 1);
}

/* len must be <= 8 and start must be a multiple of len */
static inline void tb_invalidate_phys_page_fast(tb_page_addr_t start, int len)
{
    PageDesc *p;
    int offset, b;
    p = page_find(start >> TARGET_PAGE_BITS);
    if (!p) {
        return;
    }
    if (p->code_bitmap) {
        offset = start & ~TARGET_PAGE_MASK;
        b = p->code_bitmap[offset >> 3] >> (offset & 7);
        if (b & ((1 << len) - 1)) {
            goto do_invalidate;
        }
    } else {
do_invalidate:
        tb_invalidate_phys_page_range(start, start + len, 1);
    }
}

/* add the tb in the target page and protect it if necessary */
static inline void tb_alloc_page(TranslationBlock *tb, unsigned int n, tb_page_addr_t page_addr)
{
    PageDesc *p;
    bool page_already_protected;

    tb->page_addr[n] = page_addr;
    p = page_find_alloc(page_addr >> TARGET_PAGE_BITS, 1);
    tb->page_next[n] = p->first_tb;
    page_already_protected = p->first_tb != NULL;
    p->first_tb = (TranslationBlock *)((uintptr_t)tb | n);
    invalidate_page_bitmap(p);

    /* if some code is already present, then the pages are already
       protected. So we handle the case where only the first TB is
       allocated in a physical page */
    if (!page_already_protected) {
        tlb_protect_code(page_addr);
    }
}

/* add a new TB and link it to the physical page tables. phys_page2 is
   (-1) to indicate that only one page contains the TB. */
void tb_link_page(TranslationBlock *tb, tb_page_addr_t phys_pc, tb_page_addr_t phys_page2)
{
    unsigned int h;
    TranslationBlock **ptb;

    /* Grab the mmap lock to stop another thread invalidating this TB
       before we are done.  */
    mmap_lock();
    /* add in the physical hash table */
    h = tb_phys_hash_func(phys_pc);
    ptb = &tb_phys_hash[h];
    tb->phys_hash_next = *ptb;
    *ptb = tb;

    /* add in the page list */
    tb_alloc_page(tb, 0, phys_pc & TARGET_PAGE_MASK);
    if (phys_page2 != -1) {
        tb_alloc_page(tb, 1, phys_page2);
    } else {
        tb->page_addr[1] = -1;
    }

    tb->jmp_first = (TranslationBlock *)((uintptr_t)tb | 2);
    tb->jmp_next[0] = NULL;
    tb->jmp_next[1] = NULL;

    /* init original jump addresses */
    if (tb->tb_next_offset[0] != 0xffff) {
        tb_reset_jump(tb, 0);
    }
    if (tb->tb_next_offset[1] != 0xffff) {
        tb_reset_jump(tb, 1);
    }

    mmap_unlock();
}

/* find the TB 'tb' such that tb[0].tc_ptr <= tc_ptr <
   tb[1].tc_ptr. Return NULL if not found */
TranslationBlock *tb_find_pc(uintptr_t tc_ptr)
{
    int m_min, m_max, m;
    uintptr_t v;
    TranslationBlock *tb;

    if (nb_tbs <= 0) {
        return NULL;
    }
    if (tc_ptr < (uintptr_t)code_gen_buffer || tc_ptr >= (uintptr_t)code_gen_ptr) {
        return NULL;
    }
    /* binary search (cf Knuth) */
    m_min = 0;
    m_max = nb_tbs - 1;
    while (m_min <= m_max) {
        m = (m_min + m_max) >> 1;
        tb = &tbs[m];
        v = (uintptr_t)tb->tc_ptr;
        if (v == tc_ptr) {
            return tb;
        } else if (tc_ptr < v) {
            m_max = m - 1;
        } else {
            m_min = m + 1;
        }
    }
    return &tbs[m_max];
}

static void breakpoint_invalidate(CPUState *env, target_ulong pc)
{
    TranslationBlock *tb;
    target_ulong pd;
    ram_addr_t ram_addr;
    PhysPageDesc *p;

    for (int i = 0; i < nb_tbs; ++i) {
        tb = &tbs[i];
        if (pc < tb->pc || tb->pc + tb->size < pc) {
            continue;
        }

        p = phys_page_find(tb->page_addr[0] >> TARGET_PAGE_BITS);
        if (!p) {
            pd = IO_MEM_UNASSIGNED;
        } else {
            pd = p->phys_offset;
        }
        ram_addr = (pd & TARGET_PAGE_MASK) | (pc & ~TARGET_PAGE_MASK);
        tb_invalidate_phys_page_range_inner(ram_addr, ram_addr + 1, 0, 0);
    }
}

/* Add a breakpoint.  */
int cpu_breakpoint_insert(CPUState *env, target_ulong pc, int flags, CPUBreakpoint **breakpoint)
{
    CPUBreakpoint *bp;

    bp = tlib_malloc(sizeof(*bp));

    bp->pc = pc;
    bp->flags = flags;

    /* keep all GDB-injected breakpoints in front */
    if (flags & BP_GDB) {
        QTAILQ_INSERT_HEAD(&env->breakpoints, bp, entry);
    } else {
        QTAILQ_INSERT_TAIL(&env->breakpoints, bp, entry);
    }

    breakpoint_invalidate(env, pc);

    if (breakpoint) {
        *breakpoint = bp;
    }
    return 0;
}

void interrupt_current_translation_block(CPUState *env, int exeception_type)
{
    target_ulong pc, cs_base;
    int cpu_flags;
    TranslationBlock *tb;
    int executed_instructions = -1;

    tb = tb_find_pc((uintptr_t)global_retaddr);
    if (tb != 0) {
        executed_instructions = cpu_restore_state_and_restore_instructions_count(cpu, tb, (uintptr_t)global_retaddr);
    }

    cpu_get_tb_cpu_state(cpu, &pc, &cs_base, &cpu_flags);
    tb_phys_invalidate(cpu->current_tb, -1);
    tb_gen_code(cpu, pc, cs_base, cpu_flags, 0);

    if (cpu->block_finished_hook_present) {
        tlib_on_block_finished(pc, executed_instructions);
    }

    cpu->exception_index = exeception_type;
    longjmp(cpu->jmp_env, 1);
}

/* Remove a specific breakpoint.  */
int cpu_breakpoint_remove(CPUState *env, target_ulong pc, int flags)
{
    CPUBreakpoint *bp;

    QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
        if (bp->pc == pc && bp->flags == flags) {
            cpu_breakpoint_remove_by_ref(env, bp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Remove a specific breakpoint by reference.  */
void cpu_breakpoint_remove_by_ref(CPUState *env, CPUBreakpoint *breakpoint)
{
    QTAILQ_REMOVE(&env->breakpoints, breakpoint, entry);

    breakpoint_invalidate(env, breakpoint->pc);

    tlib_free(breakpoint);
}

/* Remove all matching breakpoints. */
void cpu_breakpoint_remove_all(CPUState *env, int mask)
{
    CPUBreakpoint *bp, *next;

    QTAILQ_FOREACH_SAFE(bp, &env->breakpoints, entry, next) {
        if (bp->flags & mask) {
            cpu_breakpoint_remove_by_ref(env, bp);
        }
    }
}

bool is_interrupt_pending(CPUState *env, int mask)
{
    uint32_t interrupt_request;
    __atomic_load(&env->interrupt_request, &interrupt_request, __ATOMIC_SEQ_CST);
    return (interrupt_request & mask) != 0;
}

void set_interrupt_pending(CPUState *env, int mask)
{
    __atomic_or_fetch(&env->interrupt_request, mask, __ATOMIC_SEQ_CST);
}

void clear_interrupt_pending(CPUState *env, int mask)
{
    __atomic_and_fetch(&env->interrupt_request, ~mask, __ATOMIC_SEQ_CST);
}

/* mask must never be zero, except for A20 change call */
static void handle_interrupt(CPUState *env, int mask)
{
    set_interrupt_pending(env, mask);
    env->exit_request = 1;
}

CPUInterruptHandler cpu_interrupt_handler = handle_interrupt;

void cpu_reset_interrupt(CPUState *env, int mask)
{
    clear_interrupt_pending(env, mask);
}

static QLIST_HEAD(memory_client_list, CPUPhysMemoryClient) memory_client_list
    = QLIST_HEAD_INITIALIZER(memory_client_list);

static void cpu_notify_set_memory(target_phys_addr_t start_addr, ram_addr_t size, ram_addr_t phys_offset, bool log_dirty)
{
    CPUPhysMemoryClient *client;
    QLIST_FOREACH(client, &memory_client_list, list) {
        client->set_memory(client, start_addr, size, phys_offset, log_dirty);
    }
}

struct last_map {
    target_phys_addr_t start_addr;
    ram_addr_t size;
    ram_addr_t phys_offset;
};

void cpu_abort(CPUState *env, const char *fmt, ...)
{
    va_list ap;
    va_list ap2;
    int i;
    char s[1024];
    va_start(ap, fmt);
    va_copy(ap2, ap);
    vsprintf(s, fmt, ap);

    // ** trim CRLF at the end
    for (i = strlen(s) - 1; i > 0; i--) {
        if ((s[i] != 10) && (s[i] != 13)) {
            break;
        }
        s[i] = 0;
    }
    // **
    tlib_abort(s);
}

int get_external_mmu_phys_addr(CPUState *env, uint32_t address, int access_type,
                                                              target_phys_addr_t *phys_ptr, int *prot, int no_page_fault)
{
    bool found = false;
    int window_index = 0;
    ExtMmuRange *mmu_window = env->external_mmu_window;
    uint32_t access_type_mask = 0;
    switch (access_type) {
        case ACCESS_DATA_LOAD:
            access_type_mask = PAGE_READ;
            break;
        case ACCESS_DATA_STORE:
            access_type_mask = PAGE_WRITE;
            break;
        case ACCESS_INST_FETCH:
            access_type_mask = PAGE_EXEC;
            break;
        default:
            tlib_abortf("Incorrect access type %d", access_type);
            break;
    }

    *phys_ptr = address;
    *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    for (; window_index < MAX_EXTERNAL_MMU_RANGES; window_index++) {
        if (!mmu_window[window_index].active) {
            break;
        } else if ((mmu_window[window_index].type & access_type_mask) &&
                   (address >= mmu_window[window_index].range_start) &&
                   (address < mmu_window[window_index].range_end)) {
            found = true;
            break;
        }
    }

    if (found) {
        *phys_ptr += mmu_window[window_index].addend;
        *prot = mmu_window[window_index].priv;
        if (*prot & access_type_mask) {
            return TRANSLATE_SUCCESS;
        }
    }

    if (!no_page_fault)
    {
        // The exit_request needs to be set to prevent the cpu_exec from trying to execute the block
        cpu->exit_request = 1;
        cpu->mmu_fault = true;
        tlib_mmu_fault_external_handler(address, access_type, found ? window_index : -1);
        if(access_type != ACCESS_INST_FETCH && cpu->current_tb != NULL)
        {
            interrupt_current_translation_block(cpu, MMU_EXTERNAL_FAULT);
        }
    }
    return TRANSLATE_FAIL;
}

static inline void tlb_flush_jmp_cache(CPUState *env, target_ulong addr)
{
    unsigned int i;

    /* Discard jump cache entries for any tb which might potentially
       overlap the flushed page.  */
    i = tb_jmp_cache_hash_page(addr - TARGET_PAGE_SIZE);
    memset(&env->tb_jmp_cache[i], 0, TB_JMP_PAGE_SIZE * sizeof(TranslationBlock *));

    i = tb_jmp_cache_hash_page(addr);
    memset(&env->tb_jmp_cache[i], 0, TB_JMP_PAGE_SIZE * sizeof(TranslationBlock *));
}

static CPUTLBEntry s_cputlb_empty_entry = {
    .addr_read = -1, .addr_write = -1, .addr_code  = -1, .addend     = -1,
};

/* NOTE: if flush_global is true, also flush global entries (not
   implemented yet) */
void tlb_flush(CPUState *env, int flush_global)
{
    /* must reset current TB so that interrupts cannot modify the
       links while we are modifying them */
    env->current_tb = NULL;

    memset(env->tlb_table, 0xFF, CPU_TLB_SIZE * NB_MMU_MODES * sizeof (CPUTLBEntry));

    memset(env->tb_jmp_cache, 0, TB_JMP_CACHE_SIZE * sizeof (void *));

    env->tlb_flush_addr = -1;
    env->tlb_flush_mask = 0;
    tlb_flush_count++;
}

static inline void tlb_flush_entry(CPUTLBEntry *tlb_entry, target_ulong addr)
{
    if (addr == (tlb_entry->addr_read & (TARGET_PAGE_MASK | TLB_INVALID_MASK)) ||
        addr == (tlb_entry->addr_write & (TARGET_PAGE_MASK | TLB_INVALID_MASK)) ||
        addr == (tlb_entry->addr_code & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        *tlb_entry = s_cputlb_empty_entry;
    }
}

void tlb_flush_page(CPUState *env, target_ulong addr)
{
    int i;
    int mmu_idx;

    /* Check if we need to flush due to large pages.  */
    if ((addr & env->tlb_flush_mask) == env->tlb_flush_addr) {
        tlb_flush(env, 1);
        return;
    }
    /* must reset current TB so that interrupts cannot modify the
       links while we are modifying them */
    env->current_tb = NULL;

    addr &= TARGET_PAGE_MASK;
    i = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        tlb_flush_entry(&env->tlb_table[mmu_idx][i], addr);
    }

    tlb_flush_jmp_cache(env, addr);
}

/* update the TLBs so that writes to code in the virtual page 'addr'
   can be detected */
static void tlb_protect_code(ram_addr_t ram_addr)
{
    cpu_physical_memory_reset_dirty(ram_addr, ram_addr + TARGET_PAGE_SIZE, CODE_DIRTY_FLAG);
}

/* update the TLB so that writes in physical page 'phys_addr' are no longer
   tested for self modifying code */
static void tlb_unprotect_code_phys(CPUState *env, ram_addr_t ram_addr, target_ulong vaddr)
{
    cpu_physical_memory_set_dirty_flags(ram_addr, CODE_DIRTY_FLAG);
}

static inline void tlb_reset_dirty_range(CPUTLBEntry *tlb_entry, uintptr_t start, uintptr_t length)
{
    uintptr_t addr;
    if ((tlb_entry->addr_write & ~TARGET_PAGE_MASK) == IO_MEM_RAM) {
        addr = (tlb_entry->addr_write & TARGET_PAGE_MASK) + tlb_entry->addend;
        if ((addr - start) < length) {
            tlb_entry->addr_write = (tlb_entry->addr_write & TARGET_PAGE_MASK) | TLB_NOTDIRTY;
        }
    }
}

/* Note: start and end must be within the same ram block.  */
void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end, int dirty_flags)
{
    uintptr_t length, start1;
    int i;

    start &= TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);

    length = end - start;
    if (length == 0) {
        return;
    }
    cpu_physical_memory_mask_dirty_range(start, length, dirty_flags);

    /* we modify the TLB cache so that the dirty bit will be set again
       when accessing the range */
    start1 = (uintptr_t)get_ram_ptr(start);
    /* Check that we don't span multiple blocks - this breaks the
       address comparisons below.  */
    if ((uintptr_t)get_ram_ptr(end - 1) - start1 != (end - 1) - start) {
        tlib_abort("cpu_physical_memory_reset_dirty");
    }

    int mmu_idx;
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        for (i = 0; i < CPU_TLB_SIZE; i++) {
            tlb_reset_dirty_range(&cpu->tlb_table[mmu_idx][i], start1, length);
        }
    }
}

static inline void tlb_set_dirty1(CPUTLBEntry *tlb_entry, target_ulong vaddr)
{
    if (tlb_entry->addr_write == (vaddr | TLB_NOTDIRTY)) {
        tlb_entry->addr_write = vaddr;
    }
}

/* update the TLB corresponding to virtual page vaddr
   so that it is no longer dirty */
static inline void tlb_set_dirty(CPUState *env, target_ulong vaddr)
{
    int i;
    int mmu_idx;

    vaddr &= TARGET_PAGE_MASK;
    i = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        tlb_set_dirty1(&env->tlb_table[mmu_idx][i], vaddr);
    }
}

/* Our TLB does not support large pages, so remember the area covered by
   large pages and trigger a full TLB flush if these are invalidated.  */
static void tlb_add_large_page(CPUState *env, target_ulong vaddr, target_ulong size)
{
    target_ulong mask = ~(size - 1);

    if (env->tlb_flush_addr == (target_ulong) - 1) {
        env->tlb_flush_addr = vaddr & mask;
        env->tlb_flush_mask = mask;
        return;
    }
    /* Extend the existing region to include the new page.
       This is a compromise between unnecessary flushes and the cost
       of maintaining a full variable size TLB.  */
    mask &= env->tlb_flush_mask;
    while (((env->tlb_flush_addr ^ vaddr) & mask) != 0) {
        mask <<= 1;
    }
    env->tlb_flush_addr &= mask;
    env->tlb_flush_mask = mask;
}

static inline int is_io_accessed(CPUState *env, target_ulong vaddr)
{
    target_ulong v;
    target_ulong page_address = vaddr & ~(TARGET_PAGE_SIZE - 1);

    /* binary search (cf Knuth) */
    int32_t m_min = 0;
    int32_t m_max = env->io_access_regions_count - 1;
    int32_t m;

    while (m_min <= m_max) {
        m = (m_min + m_max) >> 1;
        v = env->io_access_regions[m];
        if (v == page_address) {
            return 1;
        } else if (vaddr < v) {
            m_max = m - 1;
        } else {
            m_min = m + 1;
        }
    }
    return 0;
}

/* Add a new TLB entry. At most one entry for a given virtual address
   is permitted. Only a single TARGET_PAGE_SIZE region is mapped, the
   supplied size is only used by tlb_flush_page.  */
void tlb_set_page(CPUState *env, target_ulong vaddr, target_phys_addr_t paddr, int prot, int mmu_idx, target_ulong size)
{
    PhysPageDesc *p;
    ram_addr_t pd;
    unsigned int index;
    target_ulong address;
    target_ulong code_address;
    uintptr_t addend;
    CPUTLBEntry *te;
    target_phys_addr_t iotlb;

    address = vaddr;

    if(size < TARGET_PAGE_SIZE)
    {
        size = TARGET_PAGE_SIZE;
        // in this special case we need to check MMU/PMP on each access
        address |= TLB_ONE_SHOT;
    }

    assert(size >= TARGET_PAGE_SIZE);
    if (size != TARGET_PAGE_SIZE) {
        tlb_add_large_page(env, vaddr, size);
    }
    p = phys_page_find(paddr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM && !(pd & IO_MEM_ROMD)) {
        /* IO memory case (romd handled later) */
        address |= TLB_MMIO;
        addend = 0;
    } else {
        addend = (uintptr_t)get_ram_ptr(pd & TARGET_PAGE_MASK);
    }
    if ((pd & ~TARGET_PAGE_MASK) <= IO_MEM_ROM) {
        /* Normal RAM.  */
        iotlb = pd & TARGET_PAGE_MASK;
        if ((pd & ~TARGET_PAGE_MASK) == IO_MEM_RAM) {
            iotlb |= IO_MEM_NOTDIRTY;
        } else {
            iotlb |= IO_MEM_ROM;
        }
    } else {
        /* IO handlers are currently passed a physical address.
           It would be nice to pass an offset from the base address
           of that region.  This would avoid having to special case RAM,
           and avoid full address decoding in every device.
           We can't use the high bits of pd for this because
           IO_MEM_ROMD uses these as a ram address.  */
        iotlb = (pd & ~TARGET_PAGE_MASK);
        if (p) {
            iotlb += p->region_offset;
        } else {
            iotlb += paddr;
        }
    }

    code_address = address;

    if (is_io_accessed(env, vaddr)) {
        iotlb = paddr;
        address |= TLB_MMIO;
    }

    index = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    env->iotlb[mmu_idx][index] = iotlb - vaddr;
    te = &env->tlb_table[mmu_idx][index];
    te->addend = addend - vaddr;
    if (prot & PAGE_READ) {
        te->addr_read = address;
    } else {
        te->addr_read = -1;
    }

    if (prot & PAGE_EXEC) {
        te->addr_code = code_address;
    } else {
        te->addr_code = -1;
    }
    if (prot & PAGE_WRITE) {
        if ((pd & ~TARGET_PAGE_MASK) == IO_MEM_ROM || (pd & IO_MEM_ROMD)) {
            /* Write access calls the I/O callback.  */
            te->addr_write = address | TLB_MMIO;
        } else if ((pd & ~TARGET_PAGE_MASK) == IO_MEM_RAM && !cpu_physical_memory_is_dirty(pd)) {
            te->addr_write = address | TLB_NOTDIRTY;
        } else {
            te->addr_write = address;
        }
    } else {
        te->addr_write = -1;
    }
}

/* register physical memory.
   For RAM, 'size' must be a multiple of the target page size.
   If (phys_offset & ~TARGET_PAGE_MASK) != 0, then it is an
   io memory page.  The address used when calling the IO function is
   the offset from the start of the region, plus region_offset.  Both
   start_addr and region_offset are rounded down to a page boundary
   before calculating this offset.  This should not be a problem unless
   the low bits of start_addr and region_offset differ.  */
void cpu_register_physical_memory_log(target_phys_addr_t start_addr, ram_addr_t size, ram_addr_t phys_offset,
                                      ram_addr_t region_offset, bool log_dirty)
{
    target_phys_addr_t addr, end_addr;
    PhysPageDesc *p;

    assert(size);
    cpu_notify_set_memory(start_addr, size, phys_offset, log_dirty);

    if (phys_offset == IO_MEM_UNASSIGNED) {
        region_offset = start_addr;
    }
    region_offset &= TARGET_PAGE_MASK;
    size = (size + TARGET_PAGE_SIZE - 1) & TARGET_PAGE_MASK;
    end_addr = start_addr + (target_phys_addr_t)size;

    addr = start_addr;
    do {
        p = phys_page_find(addr >> TARGET_PAGE_BITS);
        if (p && p->phys_offset != IO_MEM_UNASSIGNED) {
            p->phys_offset = phys_offset;
            if ((phys_offset & ~TARGET_PAGE_MASK) <= IO_MEM_ROM || (phys_offset & IO_MEM_ROMD)) {
                phys_offset += TARGET_PAGE_SIZE;
            }
        } else {
            p = phys_page_find_alloc(addr >> TARGET_PAGE_BITS, 1);
            p->phys_offset = phys_offset;
            p->region_offset = region_offset;
            if ((phys_offset & ~TARGET_PAGE_MASK) <= IO_MEM_ROM || (phys_offset & IO_MEM_ROMD)) {
                phys_offset += TARGET_PAGE_SIZE;
            }
        }
        region_offset += TARGET_PAGE_SIZE;
        addr += TARGET_PAGE_SIZE;
    } while (addr != end_addr);

    /* since each CPU stores ram addresses in its TLB cache, we must
       reset the modified entries */
    /* XXX: slow ! */
    tlb_flush(cpu, 1);
}

/* XXX: temporary until new memory mapping API */
ram_addr_t cpu_get_physical_page_desc(target_phys_addr_t addr)
{
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        return IO_MEM_UNASSIGNED;
    }
    return p->phys_offset;
}

void *get_ram_ptr(ram_addr_t addr)
{
    return tlib_guest_offset_to_host_ptr(addr);
}

ram_addr_t ram_addr_from_host(void *ptr)
{
    return tlib_host_ptr_to_guest_offset(ptr);
}

void notdirty_mem_writeb(void *opaque, target_phys_addr_t ram_addr, uint32_t val)
{
    int dirty_flags;
    dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    if (!(dirty_flags & CODE_DIRTY_FLAG)) {
        tb_invalidate_phys_page_fast(ram_addr, 1);
        dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    }
    stb_p(get_ram_ptr(ram_addr), val);
    dirty_flags |= (0xff & ~CODE_DIRTY_FLAG);
    cpu_physical_memory_set_dirty_flags(ram_addr, dirty_flags);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (dirty_flags == 0xff) {
        tlb_set_dirty(cpu, cpu->mem_io_vaddr);
    }
}

void notdirty_mem_writew(void *opaque, target_phys_addr_t ram_addr, uint32_t val)
{
    int dirty_flags;
    dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    if (!(dirty_flags & CODE_DIRTY_FLAG)) {
        tb_invalidate_phys_page_fast(ram_addr, 2);
        dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    }
    stw_p(get_ram_ptr(ram_addr), val);
    dirty_flags |= (0xff & ~CODE_DIRTY_FLAG);
    cpu_physical_memory_set_dirty_flags(ram_addr, dirty_flags);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (dirty_flags == 0xff) {
        tlb_set_dirty(cpu, cpu->mem_io_vaddr);
    }
}

void notdirty_mem_writel(void *opaque, target_phys_addr_t ram_addr, uint32_t val)
{
    int dirty_flags;
    dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    if (!(dirty_flags & CODE_DIRTY_FLAG)) {
        tb_invalidate_phys_page_fast(ram_addr, 4);
        dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    }
    stl_p(get_ram_ptr(ram_addr), val);
    dirty_flags |= (0xff & ~CODE_DIRTY_FLAG);
    cpu_physical_memory_set_dirty_flags(ram_addr, dirty_flags);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (dirty_flags == 0xff) {
        tlb_set_dirty(cpu, cpu->mem_io_vaddr);
    }
}

/* physical memory access (slow version, mainly for debug) */
void cpu_physical_memory_rw(target_phys_addr_t addr, uint8_t *buf, int len, int is_write)
{
    int l;
    uint8_t *ptr;
    uint64_t val;
    target_phys_addr_t page;
    ram_addr_t pd;
    PhysPageDesc *p;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len) {
            l = len;
        }
        p = phys_page_find(page >> TARGET_PAGE_BITS);
        if (!p) {
            pd = IO_MEM_UNASSIGNED;
        } else {
            pd = p->phys_offset;
        }

        if (is_write) {
            if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
                target_phys_addr_t addr1 = addr;
                if (p) {
                    addr1 = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
                }
                if (l >= 8 && ((addr1 & 7) == 0)) {
                    /* 64 bit write access */
                    val = ldq_p(buf);
                    tlib_write_quad_word(addr1, val);
                } else if (l >= 4 && ((addr1 & 3) == 0)) {
                    /* 32 bit write access */
                    val = ldl_p(buf);
                    tlib_write_double_word(addr1, val);
                    l = 4;
                } else if (l >= 2 && ((addr1 & 1) == 0)) {
                    /* 16 bit write access */
                    val = lduw_p(buf);
                    tlib_write_word(addr1, val);
                    l = 2;
                } else {
                    /* 8 bit write access */
                    val = ldub_p(buf);
                    tlib_write_byte(addr1, val);
                    l = 1;
                }
            } else {
                uintptr_t addr1;
                addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
                /* RAM case */
                ptr = get_ram_ptr(addr1);
                memcpy(ptr, buf, l);
                if (!cpu_physical_memory_is_dirty(addr1)) {
                    /* invalidate code */
                    tb_invalidate_phys_page_range(addr1, addr1 + l, 0);
                    /* set dirty bit */
                    cpu_physical_memory_set_dirty_flags(
                        addr1, (0xff & ~CODE_DIRTY_FLAG));
                }
            }
        } else {
            if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM && !(pd & IO_MEM_ROMD)) {
                target_phys_addr_t addr1 = addr;
                /* I/O case */
                if (p) {
                    addr1 = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
                }
                if (l >= 8 && ((addr1 & 7) == 0)) {
                    /* 64 bit read access */
                    val = tlib_read_quad_word(addr1);
                    stq_p(buf, val);
                    l = 8;
                } else if (l >= 4 && ((addr1 & 3) == 0)) {
                    /* 32 bit read access */
                    val = tlib_read_double_word(addr1);
                    stl_p(buf, val);
                    l = 4;
                } else if (l >= 2 && ((addr1 & 1) == 0)) {
                    /* 16 bit read access */
                    val = tlib_read_word(addr1);
                    stw_p(buf, val);
                    l = 2;
                } else {
                    /* 8 bit read access */
                    val = tlib_read_byte(addr1);
                    stb_p(buf, val);
                    l = 1;
                }
            } else {
                /* RAM case */
                ptr = get_ram_ptr(pd & TARGET_PAGE_MASK);
                memcpy(buf, ptr + (addr & ~TARGET_PAGE_MASK), l);
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }
}

/* used for ROM loading : can write in RAM and ROM */
void cpu_physical_memory_write_rom(target_phys_addr_t addr, const uint8_t *buf, int len)
{
    int l;
    uint8_t *ptr;
    target_phys_addr_t page;
    ram_addr_t pd;
    PhysPageDesc *p;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len) {
            l = len;
        }
        p = phys_page_find(page >> TARGET_PAGE_BITS);
        if (!p) {
            pd = IO_MEM_UNASSIGNED;
        } else {
            pd = p->phys_offset;
        }

        if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM && (pd & ~TARGET_PAGE_MASK) != IO_MEM_ROM && !(pd & IO_MEM_ROMD)) {
            /* do nothing */
        } else {
            uintptr_t addr1;
            addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
            /* ROM/RAM case */
            ptr = get_ram_ptr(addr1);
            memcpy(ptr, buf, l);
        }
        len -= l;
        buf += l;
        addr += l;
    }
}

/* warning: addr must be aligned */
static uint32_t ldl_phys_aligned(target_phys_addr_t addr)
{
    uint8_t *ptr;
    uint32_t val;
    ram_addr_t pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM && !(pd & IO_MEM_ROMD)) {
        /* I/O case */
        if (p) {
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        }
        val = tswap32(tlib_read_double_word(addr));
    } else {
        /* RAM case */
        ptr = get_ram_ptr(pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        val = ldl_p(ptr);
    }
    return val;
}

uint32_t ldl_phys(target_phys_addr_t addr)
{
    if(addr % 4 == 0)
    {
        // use a faster method
        return ldl_phys_aligned(addr);
    }

    uint32_t val;
    cpu_physical_memory_read(addr, &val, 4);
    return val;
}

/* warning: addr must be aligned */
static uint64_t ldq_phys_aligned(target_phys_addr_t addr)
{
    /* Warning! This function was rewritten to have a similar body as 
       ldl_phys_aligned. During testing we haven't found any example
       that would use this function, but there were also no observed
       changes in behavior of any binary. This function may be a good
       place to start debugging in case of bus access problems */
    uint8_t *ptr;
    uint64_t val;
    ram_addr_t pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM && !(pd & IO_MEM_ROMD)) {
        /* I/O case */
        if (p) {
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        }
        val = tswap64(tlib_read_quad_word(addr));
    } else {
        /* RAM case */
        ptr = get_ram_ptr(pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        val = ldq_p(ptr);
    }

    return val;
}

uint64_t ldq_phys(target_phys_addr_t addr)
{
    if(addr % 8 == 0)
    {
        // use a faster method
        return ldq_phys_aligned(addr);
    }

    uint64_t val;
    cpu_physical_memory_read(addr, &val, 8);
    return val;
}

/* XXX: optimize */
uint32_t ldub_phys(target_phys_addr_t addr)
{
    uint8_t val;
    cpu_physical_memory_read(addr, &val, 1);
    return val;
}

/* warning: addr must be aligned */
uint32_t lduw_phys(target_phys_addr_t addr)
{
    uint8_t *ptr;
    uint64_t val;
    ram_addr_t pd;
    PhysPageDesc *p;

    if(addr % 2 != 0)
    {
        tlib_abortf("lduw_phys address is not aligned: %lx", addr);
    }

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM && !(pd & IO_MEM_ROMD)) {
        /* I/O case */
        if (p) {
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        }
        val = tswap16(tlib_read_word(addr));
    } else {
        /* RAM case */
        ptr = get_ram_ptr(pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        val = lduw_p(ptr);
    }
    return val;
}

/* warning: addr must be aligned. The ram page is not masked as dirty
   and the code inside is not invalidated. It is useful if the dirty
   bits are used to track modified PTEs */
void stl_phys_notdirty(target_phys_addr_t addr, uint32_t val)
{
    uint8_t *ptr;
    ram_addr_t pd;
    PhysPageDesc *p;

    if(addr % 4 != 0)
    {
        tlib_abortf("stl_phys_notdirty address is not aligned: %lx", addr);
    }

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
        if (p) {
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        }
        tlib_write_double_word(addr, tswap32(val));
    } else {
        uintptr_t addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        ptr = get_ram_ptr(addr1);
        stl_p(ptr, val);
    }
}

void stq_phys_notdirty(target_phys_addr_t addr, uint64_t val)
{
    /* Warning! This function was rewritten to have a similar body as
       stl_phys_notdirty. During testing we haven't found any example
       that would use this function, but there were also no observed
       changes in behavior of any binary. This function may be a good
       place to start debugging in case of bus access problems */
    uint8_t *ptr;
    ram_addr_t pd;
    PhysPageDesc *p;

    if(addr % 8 != 0)
    {
        tlib_abortf("stq_phys_notdirty address is not aligned: %lx", addr);
    }

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
        if (p) {
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        }
        tlib_write_quad_word(addr, tswap64(val));
    } else {
        ptr = get_ram_ptr(pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        stq_p(ptr, val);
    }
}

/* warning: addr must be aligned */
static void stl_phys_aligned(target_phys_addr_t addr, uint32_t val)
{
    uint8_t *ptr;
    ram_addr_t pd;
    PhysPageDesc *p;

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
        if (p) {
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        }
        tlib_write_double_word(addr, tswap32(val));
    } else {
        uintptr_t addr1;
        addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        /* RAM case */
        ptr = get_ram_ptr(addr1);
        stl_p(ptr, val);
        if (!cpu_physical_memory_is_dirty(addr1)) {
            /* invalidate code */
            tb_invalidate_phys_page_range(addr1, addr1 + 4, 0);
            /* set dirty bit */
            cpu_physical_memory_set_dirty_flags(addr1, (0xff & ~CODE_DIRTY_FLAG));
        }
    }
}

void stl_phys(target_phys_addr_t addr, uint32_t val)
{
    if(addr % 4 == 0)
    {
        // use a faster method
        stl_phys_aligned(addr, val);
        return;
    }

    val = tswap32(val);
    cpu_physical_memory_write(addr, &val, 4);
}

/* XXX: optimize */
void stb_phys(target_phys_addr_t addr, uint32_t val)
{
    uint8_t v = val;
    cpu_physical_memory_write(addr, &v, 1);
}

/* warning: addr must be aligned */
void stw_phys(target_phys_addr_t addr, uint32_t val)
{
    uint8_t *ptr;
    ram_addr_t pd;
    PhysPageDesc *p;

    if(addr % 2 != 0)
    {
        tlib_abortf("stw_phys address is not aligned: %lx", addr);
    }

    p = phys_page_find(addr >> TARGET_PAGE_BITS);
    if (!p) {
        pd = IO_MEM_UNASSIGNED;
    } else {
        pd = p->phys_offset;
    }

    if ((pd & ~TARGET_PAGE_MASK) != IO_MEM_RAM) {
        if (p) {
            addr = (addr & ~TARGET_PAGE_MASK) + p->region_offset;
        }
        tlib_write_word(addr, tswap16(val));
    } else {
        uintptr_t addr1;
        addr1 = (pd & TARGET_PAGE_MASK) + (addr & ~TARGET_PAGE_MASK);
        /* RAM case */
        ptr = get_ram_ptr(addr1);
        stw_p(ptr, val);
        if (!cpu_physical_memory_is_dirty(addr1)) {
            /* invalidate code */
            tb_invalidate_phys_page_range(addr1, addr1 + 2, 0);
            /* set dirty bit */
            cpu_physical_memory_set_dirty_flags(addr1, (0xff & ~CODE_DIRTY_FLAG));
        }
    }
}

/* XXX: optimize */
void stq_phys(target_phys_addr_t addr, uint64_t val)
{
    val = tswap64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

#define MMUSUFFIX _cmmu
#ifdef GETPC
#undef GETPC
#endif
#define GETPC() NULL
#define SOFTMMU_CODE_ACCESS

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"
