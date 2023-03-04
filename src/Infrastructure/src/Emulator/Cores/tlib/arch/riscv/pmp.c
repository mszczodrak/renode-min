/*
 * Physical Memory Protection
 *
 * Author: Daire McNamara, daire.mcnamara@emdalo.com
 *         Ivan Griffin, ivan.griffin@emdalo.com
 *
 * This provides a RISC-V Physical Memory Protection implementation
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

#include "cpu.h"

#ifdef DEBUG_PMP
#define PMP_DEBUG(fmt, ...) \
do { tlib_printf(LOG_LEVEL_DEBUG, "pmp: " fmt, ## __VA_ARGS__); } while (0)
#else
#define PMP_DEBUG(fmt, ...) \
do {} while (0)
#endif

static void pmp_write_cfg(CPUState *env, uint32_t addr_index, uint8_t val);
static uint8_t pmp_read_cfg(CPUState *env, uint32_t addr_index);
static void pmp_update_rule(CPUState *env, uint32_t pmp_index);

/*
 * Accessor method to extract address matching type 'a field' from cfg reg
 */
static inline uint8_t pmp_get_a_field(uint8_t cfg)
{
    uint8_t a = cfg >> 3;
    return a & 0x3;
}

/*
 * Check whether a PMP is locked or not.
 */
static inline int pmp_is_locked(CPUState *env, uint32_t pmp_index)
{

    if (env->pmp_state.pmp[pmp_index].cfg_reg & PMP_LOCK) {
        return 1;
    }

    /* Top PMP has no 'next' to check */
    if ((pmp_index + 1u) >= MAX_RISCV_PMPS) {
        return 0;
    }

    /* In TOR mode, need to check the lock bit of the next pmp
     * (if there is a next)
     */
    const uint8_t a_field =
        pmp_get_a_field(env->pmp_state.pmp[pmp_index + 1].cfg_reg);
    if ((env->pmp_state.pmp[pmp_index + 1u].cfg_reg & PMP_LOCK) && (PMP_AMATCH_TOR == a_field)) {
        return 1;
    }

    return 0;
}

/*
 * Count the number of active rules.
 */
static inline uint32_t pmp_get_num_rules(CPUState *env)
{
    return env->pmp_state.num_rules;
}

/*
 * Accessor to get the cfg reg for a specific PMP/HART
 */
static inline uint8_t pmp_read_cfg(CPUState *env, uint32_t pmp_index)
{
    if (pmp_index < MAX_RISCV_PMPS) {
        return env->pmp_state.pmp[pmp_index].cfg_reg;
    }

    return 0;
}

/*
 * Accessor to set the cfg reg for a specific PMP/HART
 * Bounds checks and relevant lock bit.
 */
static void pmp_write_cfg(CPUState *env, uint32_t pmp_index, uint8_t val)
{
    if (pmp_index < MAX_RISCV_PMPS) {
        if (!pmp_is_locked(env, pmp_index)) {
            env->pmp_state.pmp[pmp_index].cfg_reg = val;
            pmp_update_rule(env, pmp_index);
        } else {
            PMP_DEBUG("Ignoring pmpcfg write - locked");
        }
    } else {
        PMP_DEBUG("Ignoring pmpcfg write - out of bounds");
    }
}

static void pmp_decode_napot(target_ulong addr, int napot_grain, target_ulong *start_addr, target_ulong *end_addr)
{
    /*
       aaaa...aaa0   8-byte NAPOT range
       aaaa...aa01   16-byte NAPOT range
       aaaa...a011   32-byte NAPOT range
       ...
       aa01...1111   2^XLEN-byte NAPOT range
       a011...1111   2^(XLEN+1)-byte NAPOT range
       0111...1111   2^(XLEN+2)-byte NAPOT range
       1111...1111   Reserved
     */
    if (addr == -1) {
        *start_addr = 0u;
        *end_addr = -1;
        return;
    } else {
        // NAPOT range equals 2^(NAPOT_GRAIN + 2)
        // Calculating base and range using 64 bit wide variables, as using
        // `target_ulong` caused overflows on RV32 when `napot_grain = 32`
        uint64_t range = ((uint64_t)2 << (napot_grain + 2)) - 1;
        uint64_t base = (addr & ((uint64_t) - 1 << (napot_grain + 1))) << 2;
        *start_addr = (target_ulong)base;
        *end_addr = (target_ulong)(base + range);
    }
}

/* Convert cfg/addr reg values here into simple 'sa' --> start address and 'ea'
 *   end address values.
 *   This function is called relatively infrequently whereas the check that
 *   an address is within a pmp rule is called often, so optimise that one
 */
static void pmp_update_rule(CPUState *env, uint32_t pmp_index)
{
    int i;

    env->pmp_state.num_rules = 0;

    uint8_t this_cfg = env->pmp_state.pmp[pmp_index].cfg_reg;
    target_ulong this_addr = env->pmp_state.pmp[pmp_index].addr_reg;
    target_ulong prev_addr = 0u;
    target_ulong napot_grain = 0u;
    target_ulong sa = 0u;
    target_ulong ea = 0u;

    if (pmp_index >= 1u) {
        prev_addr = env->pmp_state.pmp[pmp_index - 1].addr_reg;
    }

    switch (pmp_get_a_field(this_cfg)) {
    case PMP_AMATCH_OFF:
        sa = 0u;
        ea = -1;
        break;

    case PMP_AMATCH_TOR:
        sa = prev_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (this_addr << 2) - 1u;
        break;

    case PMP_AMATCH_NA4:
        sa = this_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (this_addr + 4u) - 1u;
        break;

    case PMP_AMATCH_NAPOT:
        /*  Since priv-1.11 PMP grain must be the same across all PMP regions */
        napot_grain = ctz64(~this_addr);
        if (env->privilege_architecture >= RISCV_PRIV1_11) {
            if (cpu->pmp_napot_grain == -1) {
                cpu->pmp_napot_grain = napot_grain;
            } else if (cpu->pmp_napot_grain != napot_grain) {
                napot_grain = cpu->pmp_napot_grain;
                PMP_DEBUG("Tried to set different NAPOT grains size. Size forced to match previous.");
            }
        }
        pmp_decode_napot(this_addr, napot_grain, &sa, &ea);
        break;

    default:
        sa = 0u;
        ea = 0u;
        break;
    }

    env->pmp_state.addr[pmp_index].sa = sa;
    env->pmp_state.addr[pmp_index].ea = ea;

    for (i = 0; i < MAX_RISCV_PMPS; i++) {
        const uint8_t a_field =
            pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);
        if (PMP_AMATCH_OFF != a_field) {
            env->pmp_state.num_rules++;
        }
    }

    tlb_flush(env, 1);
}

static int pmp_is_in_range(CPUState *env, int pmp_index, target_ulong addr)
{
    int result = 0;

    if ((addr >= env->pmp_state.addr[pmp_index].sa) && (addr <= env->pmp_state.addr[pmp_index].ea)) {
        result = 1;
    } else {
        result = 0;
    }

    return result;
}

/*
 * Public Interface
 */

int pmp_find_overlapping(CPUState *env, target_ulong addr, target_ulong size, int starting_index)
{
    int i;
    target_ulong pmp_sa;
    target_ulong pmp_ea;

    for (i = starting_index; i < MAX_RISCV_PMPS; i++) {
        pmp_sa = env->pmp_state.addr[i].sa;
        pmp_ea = env->pmp_state.addr[i].ea;

        if (pmp_sa < addr) {
            if (pmp_ea >= addr) {
                return i;
            }
        } else if (pmp_sa <= addr + size - 1) {
            return i;
        }
    }

    return -1;
}

/*
 * Find and return PMP configuration matching memory address
 */
int pmp_get_access(CPUState *env, target_ulong addr, target_ulong size)
{
    int i = 0;
    int ret = -1;
    target_ulong s = 0;
    target_ulong e = 0;
    pmp_priv_t allowed_privs = 0;

    /* Short cut if no rules */
    if (0 == pmp_get_num_rules(env)) {
        return PMP_READ | PMP_WRITE | PMP_EXEC;
    }

    /* 1.10 draft priv spec states there is an implicit order
         from low to high */
    for (i = 0; i < MAX_RISCV_PMPS; i++) {
        s = pmp_is_in_range(env, i, addr);
        e = pmp_is_in_range(env, i, addr + size - 1);

        /* partially inside */
        if ((s + e) == 1) {
            PMP_DEBUG("pmp violation - access is partially in inside");
            ret = 0;
            break;
        }

        /* fully inside */
        const uint8_t a_field =
            pmp_get_a_field(env->pmp_state.pmp[i].cfg_reg);
        if ((s + e) == 2 && a_field != PMP_AMATCH_OFF) {
            allowed_privs = PMP_READ | PMP_WRITE | PMP_EXEC;
            if ((env->priv != PRV_M) || pmp_is_locked(env, i)) {
                allowed_privs &= env->pmp_state.pmp[i].cfg_reg;
            }
            ret = allowed_privs;
            break;
        }
    }

    /* No rule matched */
    if (ret == -1) {
        if (env->priv == PRV_M) {
            ret = PMP_READ | PMP_WRITE | PMP_EXEC; /* Privileged spec v1.10 states if no PMP entry matches an
                                                    * M-Mode access, the access succeeds */
        } else {
            ret = 0; /* Other modes are not allowed to succeed if they don't
                      * match a rule, but there are rules.  We've checked for
                      * no rule earlier in this function. */
        }
    }

    return ret;
}

/*
 * Handle a write to a pmpcfg CSP
 */
void pmpcfg_csr_write(CPUState *env, uint32_t reg_index, target_ulong val)
{
    int i;
    uint8_t cfg_val;
    uint32_t base_offset = reg_index * sizeof(target_ulong);

    PMP_DEBUG("hart " TARGET_FMT_ld " writes: reg%d, val: 0x" TARGET_FMT_lx, env->mhartid, reg_index, val);

#if defined(TARGET_RISCV64)
    // for RV64 only even pmpcfg registers are used:
    // pmpcfg0 = [pmp0cfg, pmp1cfg, ..., pmp7cfg]
    // there is NO pmpcfg1
    // pmpcfg2 = [pmp8cfg, pmp9cfg, ..., pmp15cfg]
    // so we obtain the effective index by dividing by 2
    if (reg_index % 2 != 0) {
        PMP_DEBUG("ignoring write - incorrect address");
        return;
    }
    base_offset /= 2;
#endif

    for (i = 0; i < sizeof(target_ulong); i++) {
        cfg_val = (val >> 8 * i) & 0xff;
        pmp_write_cfg(env, base_offset + i, cfg_val);
    }
}

/*
 * Handle a read from a pmpcfg CSP
 */
target_ulong pmpcfg_csr_read(CPUState *env, uint32_t reg_index)
{
    int i;
    target_ulong cfg_val = 0;
    uint8_t val = 0;
    uint32_t base_offset = reg_index * sizeof(target_ulong);

#if defined(TARGET_RISCV64)
    // for RV64 only even pmpcfg registers are used
    // see a comment in pmpcfg_csr_write for details
    base_offset /= 2;
#endif

    for (i = 0; i < sizeof(target_ulong); i++) {
        val = pmp_read_cfg(env, base_offset + i);
        cfg_val |= (target_ulong)val << (i * 8);
    }

    PMP_DEBUG("hart " TARGET_FMT_ld "  reads: reg%d, val: 0x" TARGET_FMT_lx, env->mhartid, reg_index, cfg_val);

    return cfg_val;
}

/*
 * Handle a write to a pmpaddr CSP
 */
void pmpaddr_csr_write(CPUState *env, uint32_t addr_index, target_ulong val)
{
    PMP_DEBUG("hart " TARGET_FMT_ld " writes: addr%d, val: 0x" TARGET_FMT_lx, env->mhartid, addr_index, val);

    if (addr_index < MAX_RISCV_PMPS) {
        if (!pmp_is_locked(env, addr_index)) {
            env->pmp_state.pmp[addr_index].addr_reg = val;
            pmp_update_rule(env, addr_index);
        } else {
            PMP_DEBUG("ignoring pmpaddr write - locked");
        }
    } else {
        PMP_DEBUG("ignoring pmpaddr write - out of bounds");
    }
}

/*
 * Handle a read from a pmpaddr CSP
 */
target_ulong pmpaddr_csr_read(CPUState *env, uint32_t addr_index)
{
    PMP_DEBUG("hart " TARGET_FMT_ld "  reads: addr%d, val: 0x" TARGET_FMT_lx, env->mhartid, addr_index,
              env->pmp_state.pmp[addr_index].addr_reg);
    if (addr_index < MAX_RISCV_PMPS) {
        return env->pmp_state.pmp[addr_index].addr_reg;
    } else {
        PMP_DEBUG("ignoring read - out of bounds");
        return 0;
    }
}
