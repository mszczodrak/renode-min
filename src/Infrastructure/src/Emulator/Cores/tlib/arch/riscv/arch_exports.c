/*
 *  RISC-V interface functions
 *
 *  Copyright (c) Antmicro
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
#include "../../unwind.h"

void tlib_set_hart_id(uint32_t id)
{
    cpu->mhartid = id;
}

EXC_VOID_1(tlib_set_hart_id, uint32_t, id)

uint32_t tlib_get_hart_id()
{
    return cpu->mhartid;
}

EXC_INT_0(uint32_t, tlib_get_hart_id)

void tlib_set_mip_bit(uint32_t position, uint32_t value)
{
    pthread_mutex_lock(&cpu->mip_lock);
    // here we might have a race
    if (value) {
        cpu->mip |= ((target_ulong)1 << position);
    } else {
        cpu->mip &= ~((target_ulong)1 << position);
    }
    pthread_mutex_unlock(&cpu->mip_lock);
}

EXC_VOID_2(tlib_set_mip_bit, uint32_t, position, uint32_t, value)

void tlib_allow_feature(uint32_t feature_bit)
{
#if HOST_LONG_BITS == 32
    if(feature_bit == 'V' - 'A')
    {
        tlib_printf(LOG_LEVEL_ERROR, "Vector extension can't be enabled on 32-bit hosts.");
        return;
    }
#endif

    cpu->misa_mask |= (1L << feature_bit);
    cpu->misa |= (1L << feature_bit);

    // availability of F/D extensions
    // is indicated by a bit in MSTATUS
    if (feature_bit == 'F' - 'A' || feature_bit == 'D' - 'A') {
        set_default_mstatus();
    }
}

EXC_VOID_1(tlib_allow_feature, uint32_t, feature_bit)

void tlib_mark_feature_silent(uint32_t feature_bit, uint32_t value)
{
    if (value) {
        cpu->silenced_extensions |= (1L << feature_bit);
    } else {
        cpu->silenced_extensions &= ~(1L << feature_bit);
    }
}

EXC_VOID_2(tlib_mark_feature_silent, uint32_t, feature_bit, uint32_t, value)

uint32_t tlib_is_feature_enabled(uint32_t feature_bit)
{
    return (cpu->misa & (1L << feature_bit)) != 0;
}

EXC_INT_1(uint32_t, tlib_is_feature_enabled, uint32_t, feature_bit)

uint32_t tlib_is_feature_allowed(uint32_t feature_bit)
{
    return (cpu->misa_mask & (1L << feature_bit)) != 0;
}

EXC_INT_1(uint32_t, tlib_is_feature_allowed, uint32_t, feature_bit)

void tlib_set_privilege_architecture(int32_t privilege_architecture)
{
    if (privilege_architecture > RISCV_PRIV1_11) {
        tlib_abort("Invalid privilege architecture set. Highest suppported version is 1.11");
    }
    cpu->privilege_architecture = privilege_architecture;
}

EXC_VOID_1(tlib_set_privilege_architecture, int32_t, privilege_architecture)

uint64_t tlib_install_custom_instruction(uint64_t mask, uint64_t pattern, uint64_t length)
{
    if (cpu->custom_instructions_count == CPU_CUSTOM_INSTRUCTIONS_LIMIT) {
        // no more empty slots
        return 0;
    }

    custom_instruction_descriptor_t *ci = &cpu->custom_instructions[cpu->custom_instructions_count];

    ci->id = ++cpu->custom_instructions_count;
    ci->mask = mask;
    ci->pattern = pattern;
    ci->length = length;

    return ci->id;
}

EXC_INT_3(uint64_t, tlib_install_custom_instruction, uint64_t, mask, uint64_t, pattern, uint64_t, length)

int32_t tlib_install_custom_csr(uint64_t id)
{
    if (id > MAX_CSR_ID) {
        return -1;
    }

    int slotId = id / CSRS_PER_SLOT;
    int slotOffset = id % CSRS_PER_SLOT;

    cpu->custom_csrs[slotId] |= (1 << slotOffset);

    return 0;
}

EXC_INT_1(int32_t, tlib_install_custom_csr, uint64_t, id)

void helper_wfi(CPUState *env);
void tlib_enter_wfi()
{
    helper_wfi(cpu);
}

EXC_VOID_0(tlib_enter_wfi)

void tlib_set_csr_validation_level(uint32_t value)
{
    switch (value) {
        case CSR_VALIDATION_FULL:
        case CSR_VALIDATION_PRIV:
        case CSR_VALIDATION_NONE:
            cpu->csr_validation_level = value;
            break;

        default:
            tlib_abortf("Unexpected CSR validation level: %d", value);
    }
}

EXC_VOID_1(tlib_set_csr_validation_level, uint32_t, value)

uint32_t tlib_get_csr_validation_level()
{
    return cpu->csr_validation_level;
}

EXC_INT_0(uint32_t, tlib_get_csr_validation_level)

void tlib_set_nmi_vector(uint64_t nmi_adress, uint32_t nmi_length)
{
    if (nmi_adress > (TARGET_ULONG_MAX - nmi_length)) {
        cpu_abort(cpu, "NMIVectorAddress or NMIVectorLength value invalid. "
                  "Vector defined with these parameters will not fit in memory address space.");
    } else {
        cpu->nmi_address = (target_ulong)nmi_adress;
    }
    if (nmi_length > 32) {
        cpu_abort(cpu, "NMIVectorLength %d too big, maximum length supported is 32", nmi_length);
    } else {
        cpu->nmi_length = nmi_length;
    }
}

EXC_VOID_2(tlib_set_nmi_vector, uint64_t, nmi_adress, uint32_t, nmi_length)

void tlib_set_nmi(int32_t nmi, int32_t state)
{
    if (state) {
        cpu_set_nmi(cpu, nmi);
    } else {
        cpu_reset_nmi(cpu, nmi);
    }
}

EXC_VOID_2(tlib_set_nmi, int32_t, nmi, int32_t, state)

void tlib_allow_unaligned_accesses(int32_t allowed)
{
    cpu->allow_unaligned_accesses = allowed;
}

EXC_VOID_1(tlib_allow_unaligned_accesses, int32_t, allowed)

void tlib_set_interrupt_mode(int32_t mode)
{
    target_ulong new_value;

    switch(mode)
    {
        case INTERRUPT_MODE_AUTO:
            break;

        case INTERRUPT_MODE_DIRECT:
            new_value = (cpu->mtvec & ~0x3);
            if(cpu->mtvec != new_value)
            {
                tlib_printf(LOG_LEVEL_WARNING, "Direct interrupt mode set - updating MTVEC from 0x%x to 0x%x", cpu->mtvec, new_value);
                cpu->mtvec = new_value;
            }

            new_value = (cpu->stvec & ~0x3);
            if(cpu->stvec != new_value)
            {
                tlib_printf(LOG_LEVEL_WARNING, "Direct interrupt mode set - updating STVEC from 0x%x to 0x%x", cpu->stvec, new_value);
                cpu->stvec = new_value;
            }
            break;

        case INTERRUPT_MODE_VECTORED:
            if(cpu->privilege_architecture < RISCV_PRIV1_10)
            {
                tlib_abortf("Vectored interrupt mode not supported in the selected privilege architecture");
            }

            new_value = (cpu->mtvec & ~0x3) | 0x1;
            if(cpu->mtvec != new_value)
            {
                tlib_printf(LOG_LEVEL_WARNING, "Vectored interrupt mode set - updating MTVEC from 0x%x to 0x%x", cpu->mtvec, new_value);
                cpu->mtvec = new_value;
            }

            new_value = (cpu->stvec & ~0x3) | 0x1;
            if(cpu->stvec != new_value)
            {
                tlib_printf(LOG_LEVEL_WARNING, "Vectored interrupt mode set - updating STVEC from 0x%x to 0x%x", cpu->stvec, new_value);
                cpu->stvec = new_value;
            }

            break;

        default:
            tlib_abortf("Unexpected interrupt mode: %d", mode);
            return;
    }

    cpu->interrupt_mode = mode;
}

EXC_VOID_1(tlib_set_interrupt_mode, int32_t, mode)

uint32_t tlib_set_vlen(uint32_t vlen)
{
    // a power of 2 and not greater than VLEN_MAX
    if (((vlen - 1) & vlen) != 0 || vlen > VLEN_MAX || vlen < cpu->elen) {
        return 1;
    }
    cpu->vlenb = vlen / 8;
    return 0;
}

EXC_INT_1(uint32_t, tlib_set_vlen, uint32_t, vlen)

uint32_t tlib_set_elen(uint32_t elen)
{
    // a power of 2 and greater or equal to 8
    // current implementation puts upper bound of 64
    if (((elen - 1) & elen) != 0 || elen < 8 || elen > 64 || elen > (env->vlenb << 3)) {
        return 1;
    }
    cpu->elen = elen;
    return 0;
}

EXC_INT_1(uint32_t, tlib_set_elen, uint32_t, elen)

static bool check_vector_register_number(uint32_t regn)
{
    if (regn >= 32) {
        tlib_printf(LOG_LEVEL_ERROR, "Vector register number out of bounds");
        return 1;
    }
    return 0;
}

static bool check_vector_access(uint32_t regn, uint32_t idx)
{
    if (check_vector_register_number(regn)) {
        return 1;
    }
    if (regn >= 32) {
        tlib_printf(LOG_LEVEL_ERROR, "Vector register number out of bounds");
        return 1;
    }
    if (V_IDX_INVALID(regn)) {
        tlib_printf(LOG_LEVEL_ERROR, "Invalid vector register number (not divisible by LMUL=%d)", cpu->vlmul);
        return 1;
    }
    if (idx >= cpu->vlmax) {
        tlib_printf(LOG_LEVEL_ERROR, "Vector element index out of bounds (VLMAX=%d)", cpu->vlmax);
        return 1;
    }
    return 0;
}

uint64_t tlib_get_vector(uint32_t regn, uint32_t idx)
{
    if (check_vector_access(regn, idx)) {
        return 0;
    }

    switch (cpu->vsew) {
    case 8:
        return ((uint8_t *)V(regn))[idx];
    case 16:
        return ((uint16_t *)V(regn))[idx];
    case 32:
        return ((uint32_t *)V(regn))[idx];
    case 64:
        return ((uint64_t *)V(regn))[idx];
    default:
        tlib_printf(LOG_LEVEL_ERROR, "Unsupported SEW (%d)", cpu->vsew);
        return 0;
    }
}

EXC_INT_2(uint64_t, tlib_get_vector, uint32_t, regn, uint32_t, idx)

void tlib_set_vector(uint32_t regn, uint32_t idx, uint64_t value)
{
    if (check_vector_access(regn, idx)) {
        return;
    }
    if (value >> cpu->vsew) {
        tlib_printf(LOG_LEVEL_ERROR, "`value` (0x%llx) won't fit in vector element with SEW=%d", value, cpu->vsew);
        return;
    }

    switch (cpu->vsew) {
    case 8:
        ((uint8_t *)V(regn))[idx] = value;
        break;
    case 16:
        ((uint16_t *)V(regn))[idx] = value;
        break;
    case 32:
        ((uint32_t *)V(regn))[idx] = value;
        break;
    case 64:
        ((uint64_t *)V(regn))[idx] = value;
        break;
    default:
        tlib_printf(LOG_LEVEL_ERROR, "Unsupported SEW (%d)", cpu->vsew);
    }
}

EXC_VOID_3(tlib_set_vector, uint32_t, regn, uint32_t, idx, uint64_t, value)

uint32_t tlib_get_whole_vector(uint32_t regn, uint8_t *bytes)
{
    if (check_vector_register_number(regn)) {
        return 1;
    } else {
        memcpy(bytes, V(regn), env->vlenb);
        return 0;
    }
}

EXC_INT_2(uint32_t, tlib_get_whole_vector, uint32_t, regn, uint8_t *, bytes)

uint32_t tlib_set_whole_vector(uint32_t regn, uint8_t *bytes)
{
    if (check_vector_register_number(regn)) {
        return 1;
    } else {
        memcpy(V(regn), bytes, env->vlenb);
        return 0;
    }
}

EXC_INT_2(uint32_t, tlib_set_whole_vector, uint32_t, regn, uint8_t *, bytes)

uint32_t tlib_install_post_opcode_execution_hook(uint64_t mask, uint64_t value)
{
    if(env->post_opcode_execution_hooks_count == CPU_HOOKS_MASKS_LIMIT)
    {
        tlib_printf(LOG_LEVEL_WARNING, "Cannot install another post opcode execution hook, the maximum number of %d hooks have already been installed",
                    CPU_HOOKS_MASKS_LIMIT);
        return -1u;
    }

    uint8_t mask_index = env->post_opcode_execution_hooks_count++;
    env->post_opcode_execution_hook_masks[mask_index] = (opcode_hook_mask_t) { .mask = (target_ulong)mask, .value = (target_ulong)value };
    return mask_index;
}

EXC_INT_2(uint32_t, tlib_install_post_opcode_execution_hook, uint64_t, mask, uint64_t, value)

void tlib_enable_post_opcode_execution_hooks(uint32_t value)
{
    env->are_post_opcode_execution_hooks_enabled = !! value;
    tb_flush(env);
}

EXC_VOID_1(tlib_enable_post_opcode_execution_hooks, uint32_t, value)

void tlib_enable_post_gpr_access_hooks(uint32_t value)
{
    env->are_post_gpr_access_hooks_enabled = !! value;
    tb_flush(env);
}

EXC_VOID_1(tlib_enable_post_gpr_access_hooks, uint32_t, value)

void tlib_enable_post_gpr_access_hook_on(uint32_t register_index, uint32_t value)
{
    if(register_index > 31)
    {
        tlib_abort("Unable to add GPR access hook on register with index higher than 31");
    }
    if(value)
    {
        env->post_gpr_access_hook_mask |= 1 << register_index;
    }
    else
    {
        env->post_gpr_access_hook_mask &= ~(1u << register_index);    
    }
}

EXC_VOID_2(tlib_enable_post_gpr_access_hook_on, uint32_t, register_index, uint32_t, value)
