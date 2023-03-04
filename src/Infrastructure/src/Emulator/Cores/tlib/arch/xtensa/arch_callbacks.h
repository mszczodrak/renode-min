#ifndef ARCH_CALLBACKS_H_
#define ARCH_CALLBACKS_H_

#include <stdint.h>

void tlib_do_semihosting();
uint64_t tlib_get_cpu_time();
void tlib_timer_mod(uint32_t id, uint64_t value);

#endif
