#ifndef ATOMIC_H_
#define ATOMIC_H_

#include <pthread.h>
#include <stdint.h>
#include "targphys.h"

#define MAX_NUMBER_OF_CPUS 32

#define NO_CPU_ID          0xFFFFFFFF
#define NO_RESERVATION     -1

struct CPUState;

typedef struct address_reservation_t
{
    uint32_t locking_cpu_id;
    target_phys_addr_t address;
    uint8_t active_flag;
    uint8_t id;
} address_reservation_t;

typedef struct atomic_memory_state_t
{
    uint8_t is_mutex_initialized;
    uint8_t are_reservations_valid;

    uint32_t number_of_registered_cpus;

    uint32_t locking_cpu_id;
    uint32_t entries_count;

    int reservations_count;
    int reservations_by_cpu[MAX_NUMBER_OF_CPUS];
    address_reservation_t reservations[MAX_NUMBER_OF_CPUS];

    pthread_mutex_t global_mutex;
    pthread_cond_t global_cond;

} atomic_memory_state_t;

void register_in_atomic_memory_state(atomic_memory_state_t *sm, int id);

void acquire_global_memory_lock(struct CPUState *env);
void release_global_memory_lock(struct CPUState *env);
void clear_global_memory_lock(struct CPUState *env);

void reserve_address(struct CPUState *env, target_phys_addr_t address);
uint32_t check_address_reservation(struct CPUState *env, target_phys_addr_t address);
void register_address_access(struct CPUState *env, target_phys_addr_t address);
void cancel_reservation(struct CPUState *env);

#endif
