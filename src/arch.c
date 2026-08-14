#include "arch.h"
#include "apic.h"
#include "kernel.h"
#include "process.h"

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} descriptor_table_pointer_t;

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} idt_gate_t;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} task_state_segment_t;

_Static_assert(sizeof(task_state_segment_t) == 104u,
               "x86-64 TSS layout must remain architectural");

#define DOUBLE_FAULT_STACK_BYTES 16384u
#define USER_DATA_SELECTOR 0x18u
#define USER_CODE_SELECTOR 0x20u
#define TSS_SELECTOR 0x28u
#define IA32_EFER_MSR 0xC0000080u
#define IA32_STAR_MSR 0xC0000081u
#define IA32_LSTAR_MSR 0xC0000082u
#define IA32_FMASK_MSR 0xC0000084u
#define EFER_SYSCALL_ENABLE (1ULL << 0)
#define RFLAGS_INTERRUPT_ENABLE (1ULL << 9)
#define RFLAGS_DIRECTION (1ULL << 10)

static uint64_t gdt[7];
static idt_gate_t idt[256];
static interrupt_handler_t interrupt_handlers[256];
static task_state_segment_t tss;
static uint8_t double_fault_stack[DOUBLE_FAULT_STACK_BYTES]
    __attribute__((aligned(16)));

extern void arch_load_gdt(const descriptor_table_pointer_t *pointer);
extern void arch_load_idt(const descriptor_table_pointer_t *pointer);
extern void arch_load_task_register(void);
extern uint16_t arch_read_task_register(void);
extern void cpu_out8(uint16_t port, uint8_t value);
extern uint64_t cpu_read_cr2(void);
extern uint64_t cpu_rdmsr(uint32_t msr);
extern void cpu_wrmsr(uint32_t msr, uint64_t value);
extern void syscall_entry(void);
extern uint64_t syscall_kernel_rsp;
extern void *isr_stub_table[];

_Static_assert(sizeof(arch_user_context_t) == 144u,
               "user context layout must match syscall assembly");
_Static_assert(sizeof(interrupt_frame_t) == 176u,
               "interrupt frame layout must match ISR assembly");

static void set_idt_gate(unsigned vector, void *handler, uint8_t ist) {
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = 0x08u;
    idt[vector].ist = ist & 7u;
    idt[vector].type_attributes = 0x8Eu;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

static void initialize_tss(void) {
    uint8_t *bytes = (uint8_t *)(void *)&tss;
    for (unsigned i = 0; i < sizeof(tss); ++i) bytes[i] = 0;
    tss.ist[0] = (uint64_t)(uintptr_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.io_map_base = (uint16_t)sizeof(tss);

    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1u;
    gdt[5] = (limit & 0xFFFFu) |
             ((base & 0xFFFFFFu) << 16) |
             (0x89ULL << 40) |
             (((limit >> 16) & 0xFu) << 48) |
             (((base >> 24) & 0xFFu) << 56);
    gdt[6] = base >> 32;
}

int arch_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;
    gdt[3] = 0x00CFF2000000FFFFULL;
    gdt[4] = 0x00AFFA000000FFFFULL;
    initialize_tss();
    descriptor_table_pointer_t gdtr = {
        (uint16_t)(sizeof(gdt) - 1u),
        (uint64_t)(uintptr_t)gdt
    };
    arch_load_gdt(&gdtr);
    arch_load_task_register();

    for (unsigned i = 0; i < 256; ++i) {
        interrupt_handlers[i] = 0;
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attributes = 0;
        idt[i].offset_middle = 0;
        idt[i].offset_high = 0;
        idt[i].reserved = 0;
    }
    for (unsigned vector = 0; vector < 32; ++vector)
        set_idt_gate(vector, isr_stub_table[vector], vector == 8u ? 1u : 0u);
    set_idt_gate(ARGUS_APIC_TIMER_VECTOR, isr_stub_table[32], 0);
    set_idt_gate(ARGUS_PS2_KEYBOARD_VECTOR, isr_stub_table[33], 0);
    set_idt_gate(ARGUS_PS2_MOUSE_VECTOR, isr_stub_table[34], 0);
    set_idt_gate(ARGUS_APIC_SPURIOUS_VECTOR, isr_stub_table[35], 0);

    descriptor_table_pointer_t idtr = {
        (uint16_t)(sizeof(idt) - 1u),
        (uint64_t)(uintptr_t)idt
    };
    arch_load_idt(&idtr);

    /* Keep legacy PIC interrupts away from the APIC-owned IDT. */
    cpu_out8(0x21u, 0xFFu);
    cpu_out8(0xA1u, 0xFFu);
    return arch_read_task_register() == TSS_SELECTOR;
}

int arch_syscall_init(uint64_t kernel_stack_top) {
    if (!kernel_stack_top || (kernel_stack_top & 0xFu)) return 0;
    tss.rsp[0] = kernel_stack_top;
    syscall_kernel_rsp = kernel_stack_top;
    uint64_t efer = cpu_rdmsr(IA32_EFER_MSR);
    cpu_wrmsr(IA32_EFER_MSR, efer | EFER_SYSCALL_ENABLE);
    cpu_wrmsr(
        IA32_STAR_MSR,
        ((uint64_t)(USER_DATA_SELECTOR - 8u) << 48) | (0x08ULL << 32)
    );
    cpu_wrmsr(IA32_LSTAR_MSR, (uint64_t)(uintptr_t)syscall_entry);
    cpu_wrmsr(
        IA32_FMASK_MSR,
        RFLAGS_INTERRUPT_ENABLE | RFLAGS_DIRECTION
    );
    return (cpu_rdmsr(IA32_EFER_MSR) & EFER_SYSCALL_ENABLE) != 0 &&
           USER_CODE_SELECTOR == USER_DATA_SELECTOR + 8u;
}

void arch_trigger_double_fault(uint64_t unmapped_address) {
    idt[14].type_attributes &= 0x7Fu;
    *(volatile uint8_t *)(uintptr_t)unmapped_address = 0xDFu;
    for (;;) {}
}

int arch_on_double_fault_ist(void) {
    uint8_t stack_marker;
    uintptr_t address = (uintptr_t)&stack_marker;
    uintptr_t base = (uintptr_t)double_fault_stack;
    return address >= base && address < base + sizeof(double_fault_stack);
}

int interrupt_register(uint8_t vector, interrupt_handler_t handler) {
    if (vector < 32u || !handler || interrupt_handlers[vector]) return 0;
    interrupt_handlers[vector] = handler;
    return 1;
}

int interrupt_unregister(uint8_t vector, interrupt_handler_t handler) {
    if (!handler || interrupt_handlers[vector] != handler) return 0;
    interrupt_handlers[vector] = 0;
    return 1;
}

uint64_t interrupt_dispatch(interrupt_frame_t *frame) {
    if (!frame) return ARCH_USER_ACTION_RETURN;
    if (frame->vector == ARGUS_APIC_SPURIOUS_VECTOR)
        return ARCH_USER_ACTION_RETURN;

    if (frame->vector < 256u && interrupt_handlers[frame->vector]) {
        interrupt_handlers[frame->vector](frame);
        if ((frame->cs & 3u) == 3u)
            return process_on_user_interrupt(frame);
        return ARCH_USER_ACTION_RETURN;
    }

    uint64_t fault_address = frame->vector == 14u ? cpu_read_cr2() : 0;
    if (frame->vector < 32u && (frame->cs & 3u) == 3u)
        return process_on_user_interrupt(frame);
    kernel_exception_panic(
        frame->vector, frame->error_code, frame->rip, fault_address);
    return ARCH_USER_ACTION_FAULT;
}
