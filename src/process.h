#ifndef ARGUS_PROCESS_H
#define ARGUS_PROCESS_H

#include "arch.h"
#include "app_abi.h"
#include "paging.h"
#include <stdint.h>

#define ARGUS_PROCESS_MAX 8u

typedef enum {
    ARGUS_PROCESS_UNUSED = 0,
    ARGUS_PROCESS_READY = 1,
    ARGUS_PROCESS_RUNNING = 2,
    ARGUS_PROCESS_EXITED = 3,
} argus_process_state_t;

int process_init(const paging_info_t *kernel_space);
int process_run_self_test(void);
int process_run_ready_once(void);
uint64_t syscall_dispatch(arch_user_context_t *context);
uint32_t process_count(void);
uint32_t process_completed_count(void);
uint64_t process_syscall_count(void);
uint64_t process_yield_count(void);
uint64_t process_context_switch_count(void);
int process_address_space_isolated(void);
int process_scheduler_online(void);
uint32_t process_app_count(void);
int process_app_online(uint32_t app_id);
void process_app_set_active(uint32_t app_id);
int process_app_input(uint32_t app_id, uint8_t key);
uint64_t process_app_input_count(uint32_t app_id);
int process_app_surface(
    uint32_t app_id,
    const uint8_t **pixels,
    uint32_t *sequence
);

#endif
