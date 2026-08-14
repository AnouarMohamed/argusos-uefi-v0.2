#ifndef ARGUS_KERNEL_H
#define ARGUS_KERNEL_H

#include "boot_info.h"

void kernel_main(const boot_info_t *boot_info) __attribute__((noreturn));
void cpu_switch_stack_and_call(
    void *stack_top,
    void (*entry)(const boot_info_t *),
    const boot_info_t *boot_info
) __attribute__((noreturn));
void cpu_halt_forever(void) __attribute__((noreturn));
void kernel_exception_panic(
    uint64_t vector,
    uint64_t error_code,
    uint64_t instruction_pointer,
    uint64_t fault_address
) __attribute__((noreturn));

#endif
