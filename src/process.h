#ifndef ARGUS_PROCESS_H
#define ARGUS_PROCESS_H

#include "arch.h"
#include "paging.h"
#include <stdint.h>

#define ARGUS_PROCESS_MAX 4u

typedef enum {
    ARGUS_PROCESS_UNUSED = 0,
    ARGUS_PROCESS_READY = 1,
    ARGUS_PROCESS_RUNNING = 2,
    ARGUS_PROCESS_EXITED = 3,
} argus_process_state_t;

int process_init(const paging_info_t *kernel_space);
int process_run_self_test(void);
uint64_t syscall_dispatch(arch_user_context_t *context);
uint32_t process_count(void);
uint32_t process_completed_count(void);
uint64_t process_syscall_count(void);
uint64_t process_yield_count(void);
uint64_t process_context_switch_count(void);
int process_address_space_isolated(void);
int process_scheduler_online(void);

#endif
