#ifndef ARGUS_ARCH_H
#define ARGUS_ARCH_H

#include <stdint.h>

typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} interrupt_frame_t;

typedef void (*interrupt_handler_t)(interrupt_frame_t *frame);

int arch_init(void);
void arch_enable_interrupts(void);
void arch_trigger_double_fault(uint64_t unmapped_address) __attribute__((noreturn));
int arch_on_double_fault_ist(void);
int interrupt_register(uint8_t vector, interrupt_handler_t handler);
int interrupt_unregister(uint8_t vector, interrupt_handler_t handler);
void interrupt_dispatch(interrupt_frame_t *frame);

#endif
