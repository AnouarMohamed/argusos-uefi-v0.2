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

static uint64_t gdt[3];
static idt_gate_t idt[256];
static interrupt_handler_t interrupt_handlers[256];

extern void arch_load_gdt(const descriptor_table_pointer_t *pointer);
extern void arch_load_idt(const descriptor_table_pointer_t *pointer);
extern void cpu_out8(uint16_t port, uint8_t value);
extern uint64_t cpu_read_cr2(void);
extern void *isr_stub_table[];

static void set_idt_gate(unsigned vector, void *handler) {
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = 0x08u;
    idt[vector].ist = 0;
    idt[vector].type_attributes = 0x8Eu;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

void arch_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;
    descriptor_table_pointer_t gdtr = {
        (uint16_t)(sizeof(gdt) - 1u),
        (uint64_t)(uintptr_t)gdt
    };
    arch_load_gdt(&gdtr);

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
        set_idt_gate(vector, isr_stub_table[vector]);
    set_idt_gate(ARGUS_APIC_TIMER_VECTOR, isr_stub_table[32]);
    set_idt_gate(ARGUS_APIC_SPURIOUS_VECTOR, isr_stub_table[33]);

    descriptor_table_pointer_t idtr = {
        (uint16_t)(sizeof(idt) - 1u),
        (uint64_t)(uintptr_t)idt
    };
    arch_load_idt(&idtr);

    /* Keep legacy PIC interrupts away from the APIC-owned IDT. */
    cpu_out8(0x21u, 0xFFu);
    cpu_out8(0xA1u, 0xFFu);
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
