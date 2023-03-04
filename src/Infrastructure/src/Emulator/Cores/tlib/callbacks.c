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
#include <stdlib.h>
#include "callbacks.h"
#include "unwind.h"

DEFAULT_VOID_HANDLER1(void tlib_on_translation_block_find_slow, uint64_t pc)

void tlib_abort(char *message) __attribute__((weak));

void tlib_abort(char *message)
{
    abort();
}

DEFAULT_VOID_HANDLER2(void tlib_log, enum log_level level, char *message)

DEFAULT_INT_HANDLER1(uint64_t tlib_read_byte, uint64_t address)

DEFAULT_INT_HANDLER1(uint64_t tlib_read_word, uint64_t address)

DEFAULT_INT_HANDLER1(uint64_t tlib_read_double_word, uint64_t address)

DEFAULT_INT_HANDLER1(uint64_t tlib_read_quad_word, uint64_t address)

DEFAULT_VOID_HANDLER2(void tlib_write_byte, uint64_t address, uint64_t value)

DEFAULT_VOID_HANDLER2(void tlib_write_word, uint64_t address, uint64_t value)

DEFAULT_VOID_HANDLER2(void tlib_write_double_word, uint64_t address, uint64_t value)

DEFAULT_VOID_HANDLER2(void tlib_write_quad_word, uint64_t address, uint64_t value)

DEFAULT_INT_HANDLER2(uint32_t tlib_on_block_begin, uint64_t address, uint32_t size)

DEFAULT_VOID_HANDLER2(void tlib_on_block_finished, uint64_t pc, uint32_t executed_instructions)

void *tlib_malloc(size_t size) __attribute__((weak));

void *tlib_malloc(size_t size)
{
    return malloc(size);
}

void *tlib_realloc(void *ptr, size_t size) __attribute__((weak));

void *tlib_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void tlib_free(void *ptr) __attribute__((weak));

void tlib_free(void *ptr)
{
    free(ptr);
}

DEFAULT_VOID_HANDLER1(void tlib_on_translation_cache_size_change, uint64_t new_size)

DEFAULT_VOID_HANDLER2(void tlib_invalidate_tb_in_other_cpus, uintptr_t start, uintptr_t end)

DEFAULT_INT_HANDLER1(int32_t tlib_get_cpu_index, void)

int32_t tlib_is_on_block_translation_enabled;

void tlib_set_on_block_translation_enabled(int32_t value)
{
    tlib_is_on_block_translation_enabled = value;
}

EXC_VOID_1(tlib_set_on_block_translation_enabled, int32_t, value)

void tlib_on_block_translation(uint64_t start, uint32_t size, uint32_t flags) __attribute__((weak));

void tlib_on_block_translation(uint64_t start, uint32_t size, uint32_t flags)
{

}

DEFAULT_VOID_HANDLER3(void tlib_on_memory_access, uint64_t pc, uint32_t operation, uint64_t address)

DEFAULT_INT_HANDLER1(uint32_t tlib_is_in_debug_mode, void)

DEFAULT_VOID_HANDLER1(void tlib_on_interrupt_begin, uint64_t exception_index)

DEFAULT_VOID_HANDLER1(void tlib_on_interrupt_end, uint64_t exception_index)

DEFAULT_PTR_HANDLER1(void *tlib_guest_offset_to_host_ptr, uint64_t offset)

DEFAULT_INT_HANDLER1(uint64_t tlib_host_ptr_to_guest_offset, void *ptr)

DEFAULT_VOID_HANDLER3(void tlib_mmu_fault_external_handler, uint64_t addr, int32_t access_type, int32_t window_index)

DEFAULT_VOID_HANDLER4(void tlib_profiler_announce_stack_change, uint64_t current_address, uint64_t return_address, uint64_t instructions_count, int32_t is_frame_add)

DEFAULT_VOID_HANDLER1(void tlib_profiler_announce_context_change, uint64_t context_id)

DEFAULT_VOID_HANDLER2(void tlib_mass_broadcast_dirty, void* list_start ,int32_t size)

DEFAULT_PTR_HANDLER1(void *tlib_get_dirty_addresses_list, void *size)
