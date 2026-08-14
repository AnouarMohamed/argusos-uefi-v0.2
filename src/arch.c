#include "arch.h"
#include "apic.h"
#include "kernel.h"

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
#define TSS_SELECTOR 0x18u

static uint64_t gdt[5];
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
extern void *isr_stub_table[];

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
    gdt[3] = (limit & 0xFFFFu) |
             ((base & 0xFFFFFFu) << 16) |
             (0x89ULL << 40) |
             (((limit >> 16) & 0xFu) << 48) |
             (((base >> 24) & 0xFFu) << 56);
    gdt[4] = base >> 32;
}

int arch_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;
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

void interrupt_dispatch(interrupt_frame_t *frame) {
    if (frame->vector == ARGUS_APIC_SPURIOUS_VECTOR) return;

    if (frame->vector < 256u && interrupt_handlers[frame->vector]) {
        interrupt_handlers[frame->vector](frame);
        return;
    }

    uint64_t fault_address = frame->vector == 14u ? cpu_read_cr2() : 0;
    kernel_exception_panic(
        frame->vector, frame->error_code, frame->rip, fault_address);
}
