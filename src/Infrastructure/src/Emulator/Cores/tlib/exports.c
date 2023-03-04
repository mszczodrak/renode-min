/*
 *  Common interface for translation libraries.
 *
 *  Copyright (c) Antmicro
 *  Copyright (c) Realtime Embedded
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
#include <stdint.h>
#include "cpu.h"
#include "cpu-defs.h"
#include "tcg.h"
#include "tcg-additional.h"
#include "exec-all.h"
#include "tb-helper.h"
#include "unwind.h"

__thread struct unwind_state unwind_state;

static tcg_t stcg;

void gen_helpers(void) {
#define GEN_HELPER 2
#include "helper.h"
}

static void init_tcg()
{
    stcg.ldb = __ldb_mmu;
    stcg.ldw = __ldw_mmu;
    stcg.ldl = __ldl_mmu;
    stcg.ldq = __ldq_mmu;
    stcg.stb = __stb_mmu;
    stcg.stw = __stw_mmu;
    stcg.stl = __stl_mmu;
    stcg.stq = __stq_mmu;
    tcg_attach(&stcg);
    set_temp_buf_offset(offsetof(CPUState, temp_buf));
    int i;
    for (i = 0; i < 7; i++) {
        set_tlb_table_n_0_rwa(i, offsetof(CPUState, tlb_table[i][0].addr_read), offsetof(CPUState,
                                                                                         tlb_table[i][0].addr_write),
                              offsetof(CPUState, tlb_table[i][0].addend));
        set_tlb_table_n_0(i, offsetof(CPUState, tlb_table[i][0]));
    }
    set_tlb_entry_addr_rwu(offsetof(CPUTLBEntry, addr_read), offsetof(CPUTLBEntry, addr_write), offsetof(CPUTLBEntry, addend));
    set_sizeof_CPUTLBEntry(sizeof(CPUTLBEntry));
    set_TARGET_PAGE_BITS(TARGET_PAGE_BITS);
    attach_malloc(tlib_malloc);
    attach_realloc(tlib_realloc);
    attach_free(tlib_free);
}

// tlib_get_arch_string return an arch string that is
// *on purpose* generated compile time so that e.g.
// strings libtlib.so | grep tlib\,arch=[a-z0-9-]*\,host=[a-z0-9-]*
// can return the string.
char *tlib_get_arch_string()
{
    return "tlib,arch="
    #if defined(TARGET_ARM)
    "arm"
    #elif defined(TARGET_RISCV)
    "riscv"
    #elif defined(TARGET_PPC)
    "ppc"
    #elif defined(TARGET_XTENSA)
    "xtensa"
    #elif defined(TARGET_I386)
    "i386"
    #else
    "unknown"
    #endif
    "-"
    #if TARGET_LONG_BITS == 32
    "32"
    #elif TARGET_LONG_BITS == 64
    "64"
    #else
    "unknown"
    #endif
    "-"
    #ifdef TARGET_WORDS_BIGENDIAN
    "big"
    #else
    "little"
    #endif
    ",host="
    #ifdef HOST_I386
    "i386"
    #elif HOST_ARM
    "arm"
    #else
    "unknown"
    #endif
    "-"
    #if HOST_LONG_BITS == 32
    "32"
    #elif HOST_LONG_BITS == 64
    "64"
    #else
    "unknown"
    #endif
    ;
}

char *tlib_get_arch()
{
   #if defined(TARGET_RISCV32)
   return "rv32";
   #elif defined(TARGET_RISCV64)
   return "rv64";
   #elif defined(TARGET_ARM)
   return "arm";
   #elif defined(TARGET_I386)
   return "i386";
   #elif defined(TARGET_PPC32)
   return "ppc";
   #elif defined(TARGET_PPC64)
   return "ppc64";
   #elif defined(TARGET_XTENSA)
   return "xtensa";
   #else
   return "unknown";
   #endif
}

EXC_POINTER_0(char *, tlib_get_arch)

uint32_t maximum_block_size;

uint32_t tlib_set_maximum_block_size(uint32_t size)
{
    if(size > TCG_MAX_INSNS)
    {
        tlib_printf(LOG_LEVEL_WARNING,
            "Limiting maximum block size to %d (%" PRIu32 " requested)\n", TCG_MAX_INSNS, size);
        size = TCG_MAX_INSNS;
    }

    maximum_block_size = size;
    return maximum_block_size;
}

/* GCC 8.1 from MinGW-w64 complains about the size argument potentially being clobbered
 * by a longjmp, but it will not be used after the longjmp in question. */
#ifndef __llvm__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclobbered"
#endif
EXC_INT_1(uint32_t, tlib_set_maximum_block_size, uint32_t, size)
#ifndef __llvm__
#pragma GCC diagnostic pop
#endif

uint32_t tlib_get_maximum_block_size()
{
    return maximum_block_size;
}

EXC_INT_0(uint32_t, tlib_get_maximum_block_size)

void tlib_set_cycles_per_instruction(uint32_t count)
{
    env->cycles_per_instruction = count;
}

EXC_VOID_1(tlib_set_cycles_per_instruction, uint32_t, count)

uint32_t tlib_get_cycles_per_instruction()
{
    return env->cycles_per_instruction;
}

EXC_INT_0(uint32_t, tlib_get_cycles_per_instruction)

int32_t tlib_init(char *cpu_name)
{
    init_tcg();
    env = tlib_mallocz(sizeof(CPUState));
    cpu_exec_init(env);
    cpu_exec_init_all();
    gen_helpers();
    translate_init();
    if (cpu_init(cpu_name) != 0) {
        tlib_free(env);
        return -1;
    }
    tlib_set_maximum_block_size(TCG_MAX_INSNS);
    env->atomic_memory_state = NULL;
    return 0;
}

EXC_INT_1(int32_t, tlib_init, char *, cpu_name)

void tlib_atomic_memory_state_init(int id, uintptr_t atomic_memory_state_ptr)
{
    cpu->id = id;
    cpu->atomic_memory_state = (atomic_memory_state_t *)atomic_memory_state_ptr;
    register_in_atomic_memory_state(cpu->atomic_memory_state, id);
}

EXC_VOID_2(tlib_atomic_memory_state_init, int, id, uintptr_t, atomic_memory_state_ptr)

static void free_phys_dirty()
{
    if (dirty_ram.phys_dirty) {
        tlib_free(dirty_ram.phys_dirty);
    }
}

void tlib_dispose()
{
    tlib_arch_dispose();
    code_gen_free();
    free_all_page_descriptors();
    free_phys_dirty();
    tlib_free(cpu);
    tcg_dispose();
}

EXC_VOID_0(tlib_dispose)

// this function returns number of instructions executed since the previous call
// there is `cpu->instructions_count_total_value` that contains the cumulative value
uint64_t tlib_get_executed_instructions()
{
    uint64_t result = cpu->instructions_count_value;
    cpu->instructions_count_value = 0;
    cpu->instructions_count_limit -= result;
    return result;
}

EXC_INT_0(uint64_t, tlib_get_executed_instructions)

// `TranslationCPU` uses the number of executed instructions to calculate the elapsed virtual time.
// This number is divided by `PerformanceInMIPS` value, but may leave a remainder, that is not reflected in `TranslationCPU` state.
// To account for that, we have to report this remainder back to tlib, so that the next call to `tlib_get_executed_instructions`
// includes it in the returned value.
void tlib_reset_executed_instructions(uint32_t val)
{
    cpu->instructions_count_value = val;
    cpu->instructions_count_limit += val;
}

EXC_VOID_1(tlib_reset_executed_instructions, uint64_t, val)

uint64_t tlib_get_total_executed_instructions()
{
    return cpu->instructions_count_total_value;
}

EXC_INT_0(uint64_t, tlib_get_total_executed_instructions)

void tlib_reset()
{
    tb_flush(cpu);
    tlb_flush(cpu, 1);
    cpu_reset(cpu);
}

EXC_VOID_0(tlib_reset)

void tlib_unwind()
{
    longjmp(unwind_state.envs[unwind_state.env_idx], 1);
}

int32_t tlib_execute(uint32_t max_insns)
{
    if (cpu->instructions_count_value != 0) {
        tlib_abortf("Tried to execute cpu without reading executed instructions count first.");
    }
    cpu->instructions_count_limit = max_insns;

    uint32_t local_counter = 0;
    int32_t result = EXCP_INTERRUPT;
    while ((result == EXCP_INTERRUPT) && (cpu->instructions_count_limit > 0)) {
        result = cpu_exec(cpu);

        local_counter += cpu->instructions_count_value;
        cpu->instructions_count_limit -= cpu->instructions_count_value;
        cpu->instructions_count_value = 0;

        if(cpu->exit_request)
        {
            cpu->exit_request = 0;
            break;
        }
    }

    // we need to reset the instructions count value
    // as this is might be accessed after calling `tlib_execute`
    // to read the progress
    cpu->instructions_count_value = local_counter;

    return result;
}

EXC_INT_1(int32_t, tlib_execute, int32_t, max_insns)

int tlib_restore_context(void);

extern void *global_retaddr;

// This function needs to *not* be wrapped by the unwind.h macros to avoid leaking
// slots on the unwind stack. As an example: We enter tlib_execute_ex and do a
// PUSH_ENV, which takes env_idx from 0 to 1 (tlib_execute_ex is never executed from
// a C -> C# -> C callback, only from the CPU loop, so it will always be 0 to 1).
// Then, if tlib_execute runs to the end normally, the _ex wrapper will also run to
// its end and do a POP_ENV, but when handling a watchpoint, we won't get to the end
// of the wrapper because first we will run TlibRestartTranslationBlock on the C# side
// (in CpuThreadPauseGuard.Initialize) which is also a C import, so then env_idx goes
// from 1 to 2, and then tlib_restart_... calls interrupt_current_translation_block
// which does a longjmp() to a jmp_env defined by the CPU to go back to the CPU loop
// on the C side, so we never get to the end of tlib_restart_... . This means we've
// increased env_idx from 1 to 2, and the next wrapper return will be the one from
// tlib_execute_ex at the very beginning (no more wrappers on the way) - then we will
// decrease env_idx from 2 to 1 at the final C -> C# exit, losing one slot.
void tlib_restart_translation_block()
{
    interrupt_current_translation_block(cpu, EXCP_WATCHPOINT);
}

void tlib_set_return_request()
{
    cpu->exit_request = 1;
}

EXC_VOID_0(tlib_set_return_request)

int32_t tlib_is_wfi()
{
    return cpu->wfi;
}

EXC_INT_0(int32_t, tlib_is_wfi)

uint32_t tlib_get_page_size()
{
    return TARGET_PAGE_SIZE;
}

EXC_INT_0(uint32_t, tlib_get_page_size)

void tlib_map_range(uint64_t start_addr, uint64_t length)
{
    ram_addr_t phys_offset = start_addr;
    ram_addr_t size = length;
    //remember that phys_dirty covers the whole memory range from 0 to the end
    //of the registered memory. Most offsets are probably unused. When a new
    //region is registered before any already registered memory, the array
    //does not need to be expanded.
    uint8_t *phys_dirty;
    size_t array_start_addr, array_size, new_size;
    array_start_addr = start_addr >> TARGET_PAGE_BITS;
    array_size = size >> TARGET_PAGE_BITS;
    new_size = array_start_addr + array_size;
    if (new_size > dirty_ram.current_size) {
        phys_dirty = tlib_malloc(new_size);
        memcpy(phys_dirty, dirty_ram.phys_dirty, dirty_ram.current_size);
        if (dirty_ram.phys_dirty != NULL) {
            tlib_free(dirty_ram.phys_dirty);
        }
        dirty_ram.phys_dirty = phys_dirty;
        dirty_ram.current_size = new_size;
    }
    memset(dirty_ram.phys_dirty + array_start_addr, 0xff, array_size);
    cpu_register_physical_memory(start_addr, size, phys_offset | IO_MEM_RAM);
}

EXC_VOID_2(tlib_map_range, uint64_t, start_addr, uint64_t, length)

void tlib_unmap_range(uint64_t start, uint64_t end)
{
    uint64_t new_start;

    while (start <= end) {
        unmap_page(start);
        new_start = start + TARGET_PAGE_SIZE;
        if (new_start < start) {
            return;
        }
        start = new_start;
    }
}

EXC_VOID_2(tlib_unmap_range, uint64_t, start, uint64_t, end)

uint32_t tlib_is_range_mapped(uint64_t start, uint64_t end)
{
    PhysPageDesc *pd;

    while (start < end) {
        pd = phys_page_find((target_phys_addr_t)start >> TARGET_PAGE_BITS);
        if (pd != NULL && pd->phys_offset != IO_MEM_UNASSIGNED) {
            return 1; // at least one page of this region is mapped
        }
        start += TARGET_PAGE_SIZE;
    }
    return 0;
}

EXC_INT_2(uint32_t, tlib_is_range_mapped, uint64_t, start, uint64_t, end)

void tlib_invalidate_translation_blocks(uintptr_t start, uintptr_t end)
{
    tb_invalidate_phys_page_range_inner(start, end, 0, 0);
}

EXC_VOID_2(tlib_invalidate_translation_blocks, uintptr_t, start, uintptr_t, end)

uint64_t tlib_translate_to_physical_address(uint64_t address, uint32_t access_type)
{
    uint64_t ret = virt_to_phys(address, access_type, 1);
    if (ret == TARGET_ULONG_MAX) {
        ret = (uint64_t)-1;
    }
    return ret;
}

EXC_INT_2(uint64_t, tlib_translate_to_physical_address, uint64_t, address, uint32_t, access_type)

void tlib_set_irq(int32_t interrupt, int32_t state)
{
    if (state) {
        cpu_interrupt(cpu, interrupt);
    } else {
        cpu_reset_interrupt(cpu, interrupt);
    }
}

EXC_VOID_2(tlib_set_irq, int32_t, interrupt, int32_t, state)

int32_t tlib_is_irq_set()
{
    return cpu->interrupt_request;
}

EXC_INT_0(int32_t, tlib_is_irq_set)

void tlib_add_breakpoint(uint64_t address)
{
    cpu_breakpoint_insert(cpu, address, BP_GDB, NULL);
}

EXC_VOID_1(tlib_add_breakpoint, uint64_t, address)

void tlib_remove_breakpoint(uint64_t address)
{
    cpu_breakpoint_remove(cpu, address, BP_GDB);
}

EXC_VOID_1(tlib_remove_breakpoint, uint64_t, address)

uintptr_t translation_cache_size;

void tlib_set_translation_cache_size(uintptr_t size)
{
    translation_cache_size = size;
}

EXC_VOID_1(tlib_set_translation_cache_size, uintptr_t, size)

void tlib_invalidate_translation_cache()
{
    if (cpu) {
        tb_flush(cpu);
    }
}

EXC_VOID_0(tlib_invalidate_translation_cache)

int tlib_restore_context()
{
    uintptr_t pc;
    TranslationBlock *tb;

    pc = (uintptr_t)global_retaddr;
    tb = tb_find_pc(pc);
    if (tb == 0) {
        // this happens when PC is outside RAM or ROM
        return -1;
    }
    return cpu_restore_state_from_tb(cpu, tb, pc);
}

EXC_INT_0(int, tlib_restore_context)

void *tlib_export_state()
{
    return cpu;
}

EXC_POINTER_0(void *, tlib_export_state)

int32_t tlib_get_state_size()
{
    // Cpu state size is reported as
    // an offset of `current_tb` field
    // provided by CPU_COMMON definition.
    // It is a convention that all
    // architecture-specific, non-pointer
    // fields should be located in this
    // range. As a result this size can
    // be interpreted as an amount of bytes
    // to store during serialization.
    return (ssize_t)(&((CPUState *)0)->current_tb);
}

EXC_INT_0(int32_t, tlib_get_state_size)

void tlib_set_chaining_enabled(uint32_t val)
{
    cpu->chaining_disabled = !val;
}

EXC_VOID_1(tlib_set_chaining_enabled, uint32_t, val)

uint32_t tlib_get_chaining_enabled()
{
    return !cpu->chaining_disabled;
}

EXC_INT_0(uint32_t, tlib_get_chaining_enabled)

void tlib_set_tb_cache_enabled(uint32_t val)
{
    cpu->tb_cache_disabled = !val;
}

EXC_VOID_1(tlib_set_tb_cache_enabled, uint32_t, val)

uint32_t tlib_get_tb_cache_enabled()
{
    return !cpu->tb_cache_disabled;
}

EXC_INT_0(uint32_t, tlib_get_tb_cache_enabled)

void tlib_set_block_finished_hook_present(uint32_t val)
{
    cpu->block_finished_hook_present = !!val;
}

EXC_VOID_1(tlib_set_block_finished_hook_present, uint32_t, val)

void tlib_set_block_begin_hook_present(uint32_t val)
{
    cpu->block_begin_hook_present = !!val;
}

EXC_VOID_1(tlib_set_block_begin_hook_present, uint32_t, val)

int32_t tlib_set_return_on_exception(int32_t value)
{
    int32_t previousValue = cpu->return_on_exception;
    cpu->return_on_exception = !!value;
    return previousValue;
}

EXC_INT_1(int32_t, tlib_set_return_on_exception, int32_t, value)

void tlib_flush_page(uint64_t address)
{
    tlb_flush_page(cpu, address);
}

EXC_VOID_1(tlib_flush_page, uint64_t, address)

#if TARGET_LONG_BITS == 32
uint32_t *get_reg_pointer_32(int reg_number);
#elif TARGET_LONG_BITS == 64
uint64_t *get_reg_pointer_64(int reg_number);
#else
#error "Unknown number of bits"
#endif

uint64_t tlib_get_register_value(int reg_number)
{
    return get_register_value(reg_number);
}

EXC_INT_1(uint64_t, tlib_get_register_value, int, reg_number)

void tlib_set_register_value(int reg_number, uint64_t val)
{
    set_register_value(reg_number, val);
}

EXC_VOID_2(tlib_set_register_value, int, reg_number, uint64_t, val)

void tlib_set_interrupt_begin_hook_present(uint32_t val)
{
    cpu->interrupt_begin_callback_enabled = !!val;
}

EXC_VOID_1(tlib_set_interrupt_begin_hook_present, uint32_t, val)

void tlib_set_interrupt_end_hook_present(uint32_t val)
{
    // Supported in RISC-V architecture only
    cpu->interrupt_end_callback_enabled = !!val;
}

EXC_VOID_1(tlib_set_interrupt_end_hook_present, uint32_t, val)

void tlib_on_memory_access_event_enabled(int32_t value)
{
    cpu->tlib_is_on_memory_access_enabled = !!value;
    // In order to get all of the memory accesses we need to prevent tcg from using the tlb
    tcg_context_use_tlb(!value);
}

EXC_VOID_1(tlib_on_memory_access_event_enabled, int32_t, value)

void tlib_clean_wfi_proc_state(void)
{
    // Invalidates "Wait for interrupt" state, and makes the core ready to resume execution
    cpu->exception_index &= ~EXCP_WFI;
    cpu->wfi = 0;
}

EXC_VOID_0(tlib_clean_wfi_proc_state)

void tlib_enable_opcodes_counting(uint32_t value)
{
    cpu->count_opcodes = !!value;
}

EXC_VOID_1(tlib_enable_opcodes_counting, uint32_t, value)

uint32_t tlib_get_opcode_counter(uint32_t opcode_id)
{
    return cpu->opcode_counters[opcode_id - 1].counter;
}

EXC_INT_1(uint32_t, tlib_get_opcode_counter, uint32_t, opcode_id)

void tlib_reset_opcode_counters()
{
    for(int i = 0; i < cpu->opcode_counters_size; i++)
    {
        cpu->opcode_counters[i].counter = 0;
    }
}

EXC_VOID_0(tlib_reset_opcode_counters)

uint32_t tlib_install_opcode_counter(uint32_t opcode, uint32_t mask)
{
    if(cpu->opcode_counters_size == MAX_OPCODE_COUNTERS)
    {
        // value 0 should be interpreted as an error;
        // code calling `tlib_install_opcode_counter` should
        // handle this properly (and e.g., log an error message)
        return 0;
    }

    cpu->opcode_counters[cpu->opcode_counters_size].opcode = opcode;
    cpu->opcode_counters[cpu->opcode_counters_size].mask = mask;
    cpu->opcode_counters_size++;

    return cpu->opcode_counters_size;
}

EXC_INT_2(uint32_t, tlib_install_opcode_counter, uint32_t, opcode, uint32_t, mask)

void tlib_enable_guest_profiler(int value)
{
    if(cpu->guest_profiler_enabled == value)
    {
        return;
    }

    // When the state of the guest profiler is changed we have to
    // invalidate the cache for two reasons:
    // When the profiler is enabled: to ensure that no block that don't
    // signal stack changes will be used (function calls will not be detected)
    // When the profiler is disabled: to ensure that no blocks that 
    // signal stack changes will be used (events will be sent to a null object)
    tlib_invalidate_translation_cache();
    cpu->guest_profiler_enabled = !!value;
}
EXC_VOID_1(tlib_enable_guest_profiler, int32_t, value)

uint32_t tlib_get_current_tb_disas_flags()
{
    if (cpu->current_tb == NULL) {
        return 0xFFFFFFFF;
    }

    return cpu->current_tb->disas_flags;
}

EXC_INT_0(uint32_t, tlib_get_current_tb_disas_flags)

void tlib_set_page_io_accessed(uint64_t address)
{
    if(env->io_access_regions_count == MAX_IO_ACCESS_REGIONS_COUNT)
    {
        tlib_abortf("Couldn't register an IO accessible page 0x%x", address);
    }

    target_ulong page_address = address & ~(TARGET_PAGE_SIZE - 1);

    int i, j;
    for(i = 0; i < env->io_access_regions_count; i++)
    {
        if(env->io_access_regions[i] == page_address)
        {
            // it's already here, just break
            return;
        }

        // since regions are sorted ascending, this is the right place to put the new entry
        if(env->io_access_regions[i] > page_address)
        {
            break;
        }
    }

    for(j = env->io_access_regions_count; j > i; j--)
    {
        env->io_access_regions[j] = env->io_access_regions[j - 1];
    }

    env->io_access_regions[i] = page_address;
    env->io_access_regions_count++;

    tlb_flush_page(env, address);
}

EXC_VOID_1(tlib_set_page_io_accessed, uint64_t, address)

void tlib_clear_page_io_accessed(uint64_t address)
{
    target_ulong page_address = address & ~(TARGET_PAGE_SIZE - 1);

    int i, j;
    for(i = 0; i < env->io_access_regions_count; i++)
    {
        if(env->io_access_regions[i] == page_address)
        {
            break;
        }
    }

    if(i == env->io_access_regions_count)
    {
        // it was not marked as IO
        return;
    }

    for(j = env->io_access_regions_count - 1; j > i; j--)
    {
        env->io_access_regions[j - 1] = env->io_access_regions[j];
    }

    env->io_access_regions_count--;
    tlb_flush_page(env, address);
}

EXC_VOID_1(tlib_clear_page_io_accessed, uint64_t, address)

#define ASSERT_EXTERNAL_MMU_ENABLED                                                                                \
if(!cpu->external_mmu_enabled)                                                                                     \
{                                                                                                                  \
    tlib_abort("Setting the external MMU parameters, when it is not enabled. Enable it first");                    \
}

#define ASSERT_WINDOW_ACTIVE(index)                                                                                \
if(!cpu->external_mmu_window[index].active)                                                                        \
{                                                                                                                  \
    tlib_printf(LOG_LEVEL_ERROR, "Trying to configure an inactive window. Window needs to be activated first");    \
}

#define ASSERT_WINDOW_IN_RANGE(index)                                                                              \
if(index > MAX_EXTERNAL_MMU_RANGES)                                                                                \
{                                                                                                                  \
    tlib_abort("Trying to access an unexisting MMU window. Index too high");                                       \
}

#define ASSERT_ALIGNED_TO_PAGE_SIZE(addr) \
if(addr & (~TARGET_PAGE_MASK)) tlib_abortf("MMU ranges must be aligned to the page size (0x%lx), the address 0x%lx is not.", TARGET_PAGE_SIZE, addr);

#define ASSERT_NO_OVERLAP(value, window_type)                                                                                                                \
for(int window_index = 0; window_index < MAX_EXTERNAL_MMU_RANGES; window_index++)                                                                            \
{                                                                                                                                                            \
    ExtMmuRange* current_window = &cpu->external_mmu_window[window_index];                                                                                   \
    if(!current_window->active)                                                                                                                              \
    {                                                                                                                                                        \
        break;                                                                                                                                               \
    }                                                                                                                                                        \
    if(value >= current_window->range_start && value < current_window->range_end && current_window->type & window_type)                                      \
    {                                                                                                                                                        \
        tlib_printf(LOG_LEVEL_DEBUG, "The addr 0x%lx is already a part of the MMU window of the same type with index %d. Resulting range will overlap!",     \
                    value, window_index);                                                                                                                    \
        break;                                                                                                                                               \
    }                                                                                                                                                        \
}                                                                                                                                                            \

uint32_t tlib_get_mmu_windows_count(void)
{
    return MAX_EXTERNAL_MMU_RANGES;
}
EXC_INT_0(uint32_t, tlib_get_mmu_windows_count)

void tlib_enable_external_window_mmu(uint32_t value)
{
#ifndef TARGET_RISCV
    tlib_printf(LOG_LEVEL_WARNING, "Enabled the external MMU. Please note that this feature is experimental on this platform");
#endif
    cpu->external_mmu_enabled = !! value;
}
EXC_VOID_1(tlib_enable_external_window_mmu, uint32_t, value)

void tlib_reset_mmu_window(uint32_t index)
{
    ASSERT_WINDOW_IN_RANGE(index)
    ExtMmuRange *mmu_array = cpu->external_mmu_window;
    memset((void *)(mmu_array + index), 0, sizeof(ExtMmuRange));
}
EXC_VOID_1(tlib_reset_mmu_window, uint32_t, index)

int32_t tlib_acquire_mmu_window(uint32_t type)
{
    ASSERT_EXTERNAL_MMU_ENABLED
    for (int window_index = 0; window_index < MAX_EXTERNAL_MMU_RANGES; window_index++) {
        if (!cpu->external_mmu_window[window_index].active) {
            cpu->external_mmu_window[window_index].active = true;
            cpu->external_mmu_window[window_index].type = (uint8_t)type;
            return window_index;
        }
    }
    // Failed
    return -1;
}
EXC_INT_1(int32_t, tlib_acquire_mmu_window, uint32_t, type)

void tlib_set_mmu_window_start(uint32_t index, uint64_t addr_start)
{
    ASSERT_EXTERNAL_MMU_ENABLED
    ASSERT_WINDOW_ACTIVE(index)
    ASSERT_ALIGNED_TO_PAGE_SIZE(addr_start)
#ifdef DEBUG
    ASSERT_NO_OVERLAP(addr_start, cpu->external_mmu_window[index].type)
#endif
    cpu->external_mmu_window[index].range_start = addr_start;
}
EXC_VOID_2(tlib_set_mmu_window_start, uint32_t, index, uint64_t, addr_start)

void tlib_set_mmu_window_end(uint32_t index, uint64_t addr_end)
{
    ASSERT_EXTERNAL_MMU_ENABLED
    ASSERT_WINDOW_ACTIVE(index)
    ASSERT_ALIGNED_TO_PAGE_SIZE(addr_end)
#ifdef DEBUG
    ASSERT_NO_OVERLAP(addr_end, cpu->external_mmu_window[index].type)
#endif
    cpu->external_mmu_window[index].range_end = addr_end;
}
EXC_VOID_2(tlib_set_mmu_window_end, uint32_t, index, uint64_t, addr_end)

void tlib_set_window_privileges(uint32_t index, int32_t privileges)
{
    ASSERT_EXTERNAL_MMU_ENABLED
    ASSERT_WINDOW_ACTIVE(index)
    cpu->external_mmu_window[index].priv = privileges;
}
EXC_VOID_2(tlib_set_window_privileges, uint32_t, index, int32_t, privileges)

void tlib_set_mmu_window_addend(uint32_t index, uint64_t addend)
{
    ASSERT_EXTERNAL_MMU_ENABLED
    ASSERT_WINDOW_ACTIVE(index)
    cpu->external_mmu_window[index].addend = addend;
}
EXC_VOID_2(tlib_set_mmu_window_addend, uint32_t, index, uint64_t, addend)

uint64_t tlib_get_mmu_window_start(uint32_t index)
{
    ASSERT_WINDOW_IN_RANGE(index)
    return cpu->external_mmu_window[index].range_start;
}
EXC_INT_1(uint64_t, tlib_get_mmu_window_start, uint32_t, index)

uint64_t tlib_get_mmu_window_end(uint32_t index)
{
    ASSERT_WINDOW_IN_RANGE(index)
    return cpu->external_mmu_window[index].range_end;
}
EXC_INT_1(uint64_t, tlib_get_mmu_window_end, uint32_t, index)

int tlib_get_window_privileges(uint32_t index)
{
    ASSERT_WINDOW_IN_RANGE(index)
    return cpu->external_mmu_window[index].priv;
}
EXC_INT_1(uint64_t, tlib_get_window_privileges, uint32_t, index)

uint64_t tlib_get_mmu_window_addend(uint32_t index)
{
    ASSERT_WINDOW_IN_RANGE(index)
    return cpu->external_mmu_window[index].addend;
}
EXC_INT_1(uint64_t, tlib_get_mmu_window_addend, uint32_t, index)

void tlib_raise_exception(uint32_t exception)
{
    // note: this function does interrupt
    // the execution of the block, so
    // externally raised exceptions might
    // not be handled precisely
    env->exception_index = exception;
    env->exit_request = 1;
}
EXC_VOID_1(tlib_raise_exception, uint32_t, exception)

void tlib_set_broadcast_dirty(int enable)
{
    cpu->tb_broadcast_dirty = enable == 0 ? false : true;
}

EXC_VOID_1(tlib_set_broadcast_dirty, int32_t, enable)
