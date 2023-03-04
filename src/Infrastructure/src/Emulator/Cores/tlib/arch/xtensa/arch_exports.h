#ifndef ARCH_EXPORTS_H_
#define ARCH_EXPORTS_H_

#include <stdint.h>

void tlib_set_irq_pending_bit(uint32_t irq, uint32_t value);
void tlib_update_execution_mode(uint32_t mode);

#endif
