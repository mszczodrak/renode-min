/*
 *  Host code generation
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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "tcg-op.h"
#include "debug.h"

#include <global_helper.h>
#define GEN_HELPER 1
#include <global_helper.h>

int gen_new_label(void);

extern TCGv_ptr cpu_env;
extern CPUState *cpu;

static int exit_no_hook_label;
static int block_header_interrupted_label;

CPUBreakpoint *process_breakpoints(CPUState *env, target_ulong pc)
{
    CPUBreakpoint *bp;
    QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
        if (bp->pc == pc) {
            return bp;
        }
    }
    return NULL;
}

static inline void gen_update_instructions_count(TranslationBlock *tb)
{
    // Assumption: tb == cpu->current_tb when this block is executed
    // This is ensured by the prepare_block_for_execution helper
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 icount = tcg_temp_new_i64();
    TCGv_ptr tb_pointer = tcg_const_ptr((tcg_target_long)tb);

    // (uint32_t) tb->icount
    tcg_gen_ld32u_i64(icount, tb_pointer, offsetof(TranslationBlock, icount));

    // (uint32_t) cpu->instructions_count_value += tb->icount
    tcg_gen_ld32u_i64(tmp, cpu_env, offsetof(CPUState, instructions_count_value));
    tcg_gen_add_i64(tmp, tmp, icount);
    tcg_gen_st32_i64(tmp, cpu_env, offsetof(CPUState, instructions_count_value));

    // (uint64_t) cpu->instructions_count_total_value += tb->icount
    tcg_gen_ld_i64(tmp, cpu_env, offsetof(CPUState, instructions_count_total_value));
    tcg_gen_add_i64(tmp, tmp, icount);
    tcg_gen_st_i64(tmp, cpu_env, offsetof(CPUState, instructions_count_total_value));

    // (uint32_t) tb->instructions_count_dirty = 1
    tcg_gen_movi_i64(tmp, 1);
    tcg_gen_st32_i64(tmp, tb_pointer, offsetof(TranslationBlock, instructions_count_dirty));

    tcg_temp_free_ptr(tb_pointer);
    tcg_temp_free_i64(icount);
    tcg_temp_free_i64(tmp);
}

static inline void gen_block_header(TranslationBlock *tb)
{
    TCGv_i32 flag;
    exit_no_hook_label = gen_new_label();
    TCGv_ptr tb_pointer = tcg_const_ptr((tcg_target_long)tb);
    flag = tcg_temp_local_new_i32();
    gen_helper_prepare_block_for_execution(flag, tb_pointer);
    tcg_temp_free_ptr(tb_pointer);
    tcg_gen_brcondi_i32(TCG_COND_NE, flag, 0, exit_no_hook_label);
    tcg_temp_free_i32(flag);

    if (cpu->block_begin_hook_present) {
        TCGv_i32 result = tcg_temp_new_i32();
        gen_helper_block_begin_event(result);
        block_header_interrupted_label = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_EQ, result, 0, block_header_interrupted_label);
        tcg_temp_free_i32(result);
    }

    gen_update_instructions_count(tb);
}

static void gen_block_finished_hook(TranslationBlock *tb, uint32_t instructions_count)
{
    if (cpu->block_finished_hook_present) {
        TCGv first_instruction = tcg_const_tl(tb->pc);
        TCGv_i32 executed_instructions = tcg_const_i32(instructions_count);
        gen_helper_block_finished_event(first_instruction, executed_instructions);
        tcg_temp_free_i32(executed_instructions);
        tcg_temp_free(first_instruction);
    }
}

static void gen_exit_tb_inner(TranslationBlock *tb, int n, uint32_t instructions_count)
{
    gen_block_finished_hook(tb, instructions_count);
    tcg_gen_exit_tb((uintptr_t)tb + n);
}

static void gen_interrupt_tb(TranslationBlock *tb, int n)
{
    // since the block was interrupted before executing any instruction we return 0
    gen_exit_tb_inner(tb, n, 0);
}

void gen_exit_tb(TranslationBlock *tb, int n)
{
    gen_exit_tb_inner(tb, n, tb->icount);
}

void gen_exit_tb_no_chaining(TranslationBlock *tb)
{
    gen_block_finished_hook(tb, tb->icount);
    tcg_gen_exit_tb(0);
}

static inline void gen_block_footer(TranslationBlock *tb)
{
    if (tlib_is_on_block_translation_enabled) {
        tlib_on_block_translation(tb->pc, tb->size, tb->disas_flags);
    }

    int finish_label = gen_new_label();
    gen_exit_tb(tb, EXIT_TB_FORCE);
    tcg_gen_br(finish_label);


    if (cpu->block_begin_hook_present) {
        gen_set_label(block_header_interrupted_label);
        gen_interrupt_tb(tb, EXIT_TB_FORCE);
        tcg_gen_br(finish_label);
    }

    gen_set_label(exit_no_hook_label);
    tcg_gen_exit_tb((uintptr_t)tb | EXIT_TB_FORCE);

    gen_set_label(finish_label);
    *gen_opc_ptr = INDEX_op_end;
}

static inline uint32_t get_max_tb_instruction_count(CPUState *env)
{
    uint32_t current_instructions_count_limit = env->instructions_count_limit - env->instructions_count_value;
    return maximum_block_size > current_instructions_count_limit ? current_instructions_count_limit : maximum_block_size;
}

static void cpu_gen_code_inner(CPUState *env, TranslationBlock *tb)
{
    DisasContext dcc;
    CPUBreakpoint *bp;
    DisasContextBase *dc = (DisasContextBase *)&dcc;

    uint32_t max_tb_icount = get_max_tb_instruction_count(env);

    tb->icount = 0;
    tb->was_cut = false;
    tb->size = 0;
    dc->tb = tb;
    dc->is_jmp = DISAS_NEXT;
    dc->pc = tb->pc;
    dc->guest_profile = env->guest_profiler_enabled;

    gen_block_header(tb);
    setup_disas_context(dc, env);
    tcg_clear_temp_count();
    UNLOCK_TB(tb);
    while (1) {
        CHECK_LOCKED(tb);
        if (unlikely(!QTAILQ_EMPTY(&env->breakpoints))) {
            bp = process_breakpoints(env, dc->pc);
            if (bp != NULL && gen_breakpoint(dc, bp)) {
                break;
            }
        }
        tb->prev_size = tb->size;

        int do_break = 0;
        tb->icount++;
        if (!gen_intermediate_code(env, dc)) {
            do_break = 1;
        }
        if (tcg_check_temp_count()) {
            tlib_abortf("TCG temps leak detected at PC %08X", dc->pc);
        }
        if (do_break) {
            break;
        }
        if (dc->is_jmp != DISAS_NEXT) {
            break;
        }
        if ((gen_opc_ptr - tcg->gen_opc_buf) >= OPC_MAX_SIZE) {
            break;
        }
        if (tb->icount >= max_tb_icount) {
            tb->was_cut = true;
            break;
        }
    }
    tb->disas_flags = gen_intermediate_code_epilogue(env, dc);
    gen_block_footer(tb);
}

/* Encode VAL as a signed leb128 sequence at P.
   Return P incremented past the encoded value.  */
static uint8_t *encode_sleb128(uint8_t *p, target_long val)
{
    int more, byte;

    do {
        byte = val & 0x7f;
        val >>= 7;
        more = !((val == 0 && (byte & 0x40) == 0)
                 || (val == -1 && (byte & 0x40) != 0));
        if (more) {
            byte |= 0x80;
        }
        *p++ = byte;
    } while (more);

    return p;
}

/* Decode a signed leb128 sequence at *PP; increment *PP past the
   decoded value.  Return the decoded value.  */
static target_long decode_sleb128(uint8_t **pp)
{
    uint8_t *p = *pp;
    target_long val = 0;
    int byte, shift = 0;

    do {
        byte = *p++;
        val |= (target_ulong)(byte & 0x7f) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < TARGET_LONG_BITS && (byte & 0x40)) {
        val |= -(target_ulong)1 << shift;
    }

    *pp = p;
    return val;
}

/* Encode the data collected about the instructions while compiling TB.
   Place the data at BLOCK, and return the number of bytes consumed.

   This data will be saved after the end of the generated host code,
   see cpu_gen_code. We need to save it because otherwise we would need
   to retranslate the TB to find out the target PC (and other associated data)
   corresponding to a particular host PC, which we need to do to restore
   the CPU state up to a certain point within a block.

   The logical table consists of TARGET_INSN_START_WORDS target_ulong's,
   which come from the target's insn_start data, followed by a uintptr_t
   which comes from the host pc of the end of the code implementing the insn.
   The first word of insn_start data is always the guest PC of the insn.

   Each line of the table is encoded as sleb128 deltas from the previous
   line.  The seed for the first line is { tb->pc, 0..., tb->tc_ptr }.
   That is, the first column is seeded with the guest pc, the last column
   with the host pc, and the middle columns with zeros.

   See cpu_restore_state_from_tb for how this is decoded. */

static int encode_search(TranslationBlock *tb, uint8_t *block)
{
    uint8_t *p = block;
    int i, j, n;

    tb->tc_search = block;

    for (i = 0, n = tb->icount; i < n; ++i) {
        target_ulong prev;

        for (j = 0; j < TARGET_INSN_START_WORDS; ++j) {
            if (i == 0) {
                prev = (j == 0 ? tb->pc : 0);
            } else {
                prev = tcg->gen_insn_data[i - 1][j];
            }
            p = encode_sleb128(p, tcg->gen_insn_data[i][j] - prev);
        }
        prev = (i == 0 ? 0 : tcg->gen_insn_end_off[i - 1]);
        p = encode_sleb128(p, tcg->gen_insn_end_off[i] - prev);
    }

    return p - block;
}

/* '*gen_code_size_ptr' contains the size of the generated code (host
   code), '*search_size_ptr' contains the size of the search data.
 */
void cpu_gen_code(CPUState *env, TranslationBlock *tb, int *gen_code_size_ptr, int *search_size_ptr)
{
    TCGContext *s = tcg->ctx;
    uint8_t *gen_code_buf;
    int gen_code_size, search_size;

    tcg_func_start(s);
    cpu_gen_code_inner(env, tb);

    /* generate machine code */
    gen_code_buf = tb->tc_ptr;
    tb->tb_next_offset[0] = 0xffff;
    tb->tb_next_offset[1] = 0xffff;

    s->tb_next_offset = tb->tb_next_offset;
    s->tb_jmp_offset = tb->tb_jmp_offset;
    s->tb_next = NULL;

    gen_code_size = tcg_gen_code(s, gen_code_buf);
    *gen_code_size_ptr = gen_code_size;

    search_size = encode_search(tb, gen_code_buf + gen_code_size);
    *search_size_ptr = search_size;
}

/* The cpu state corresponding to 'searched_pc' is restored.
 */
int cpu_restore_state_from_tb(CPUState *env, TranslationBlock *tb, uintptr_t searched_pc)
{
    target_ulong data[TARGET_INSN_START_WORDS] = { tb->pc };
    uintptr_t host_pc = (uintptr_t)tb->tc_ptr;
    uint8_t *p = tb->tc_search;
    int i, j, num_insns = tb->icount;

    if (searched_pc < host_pc) {
        return -1;
    }

    /* Reconstruct the stored insn data while looking for the point at
       which the end of the insn exceeds the searched_pc.  */
    for (i = 1; i <= num_insns; ++i) {
        for (j = 0; j < TARGET_INSN_START_WORDS; ++j) {
            data[j] += decode_sleb128(&p);
        }
        host_pc += decode_sleb128(&p);
        if (host_pc > searched_pc) {
            goto found;
        }
    }
    return -1;

 found:
    restore_state_to_opc(env, tb, data);

    return i;
}

int cpu_restore_state_and_restore_instructions_count(CPUState *env, TranslationBlock *tb, uintptr_t searched_pc)
{
    int executed_instructions = cpu_restore_state_from_tb(env, tb, searched_pc);
    if (executed_instructions != -1 && tb->instructions_count_dirty) {
        cpu->instructions_count_value -= (tb->icount - executed_instructions);
        cpu->instructions_count_total_value -= (tb->icount - executed_instructions);
        tb->instructions_count_dirty = 0;
    }
    return executed_instructions;
}

void cpu_restore_state(CPUState *env, void *retaddr) {
  TranslationBlock *tb;
  uintptr_t pc = (uintptr_t)retaddr;

  if (pc) {
    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
      /* the PC is inside the translated code. It means that we have
         a virtual CPU fault */
      cpu_restore_state_and_restore_instructions_count(env, tb, pc);
    }
  }
}

void generate_opcode_count_increment(CPUState *env, uint64_t opcode)
{
    uint64_t masked_opcode;
    for (uint32_t i = 0; i < env->opcode_counters_size; i++) {
        // mask out non-opcode fields
        masked_opcode = opcode & env->opcode_counters[i].mask;
        if (env->opcode_counters[i].opcode == masked_opcode) {
            TCGv_i32 p = tcg_const_i32(i);
            gen_helper_count_opcode_inner(p);
            tcg_temp_free_i32(p);
            break;
        }
    }
}

inline uint64_t get_register_value(int reg_number)
{
#if TARGET_LONG_BITS == 32
    uint32_t *ptr = get_reg_pointer_32(reg_number);
    if (ptr == NULL) {
        tlib_abortf("Read from undefined CPU register number %d detected", reg_number);
        return 0;
    }
    return (uint64_t)*ptr;
#elif TARGET_LONG_BITS == 64
    uint64_t *ptr = get_reg_pointer_64(reg_number);
    if (ptr == NULL) {
        tlib_abortf("Read from undefined CPU register number %d detected", reg_number);
        return 0;
    }
    return *ptr;
#else
#error "Unknown number of bits"
#endif
}

inline void set_register_value(int reg_number, uint64_t val)
{
    #if TARGET_LONG_BITS == 32
        uint32_t *ptr = get_reg_pointer_32(reg_number);
        if (ptr == NULL) {
            tlib_abortf("Write to undefined CPU register number %d detected", reg_number);
            return;
        }
        *ptr = val & 0xFFFFFFFF;
    #elif TARGET_LONG_BITS == 64
        uint64_t *ptr = get_reg_pointer_64(reg_number);
        if (ptr == NULL) {
            tlib_abortf("Write to undefined CPU register number %d detected", reg_number);
            return;
        }
        *ptr = val;
    #else
    #error "Unknown number of bits"
    #endif
}

void tlib_announce_stack_change(target_ulong address, int change_type)
{
    #ifdef SUPPORTS_GUEST_PROFILING
    tlib_profiler_announce_stack_change(address, get_register_value(RA), cpu->instructions_count_total_value, change_type);
    #else
    tlib_abortf("This architecture does not support the profiler");
    #endif
}

void tlib_announce_context_change(target_ulong context_id)
{
    #ifdef SUPPORTS_GUEST_PROFILING
    tlib_profiler_announce_context_change(context_id);
    #else
    tlib_abortf("This architecture does not support the profiler");
    #endif
}
