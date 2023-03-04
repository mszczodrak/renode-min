#ifndef ARCH_EXPORTS_H_
#define ARCH_EXPORTS_H_

#include <stdint.h>

uint32_t tlib_get_cpu_id(void);
uint32_t tlib_get_it_state(void);
uint32_t tlib_evaluate_condition_code(uint32_t);

void tlib_set_cpu_id(uint32_t value);
void tlib_toggle_fpu(int32_t enabled);
void tlib_set_sev_on_pending(int32_t);
void tlib_set_event_flag(int value);

#ifdef TARGET_PROTO_ARM_M
void tlib_set_interrupt_vector_base(uint32_t address);
uint32_t tlib_get_interrupt_vector_base(void);

uint32_t tlib_get_xpsr(void);
#endif

#endif
