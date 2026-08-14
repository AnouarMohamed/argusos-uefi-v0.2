#include "apic.h"

#define IA32_APIC_BASE_MSR 0x1Bu
#define APIC_ENABLE (1ULL << 11)
#define APIC_X2_MODE (1ULL << 10)

#define APIC_TPR          0x080u
#define APIC_EOI          0x0B0u
#define APIC_SVR          0x0F0u
#define APIC_LVT_TIMER    0x320u
#define APIC_INITIAL_COUNT 0x380u
#define APIC_DIVIDE_CONFIG 0x3E0u

extern uint64_t cpu_rdmsr(uint32_t msr);
extern void cpu_wrmsr(uint32_t msr, uint64_t value);

static volatile uint32_t *lapic;
static int x2apic_mode;
static volatile uint64_t timer_ticks;

static uint32_t apic_read(uint32_t offset) {
    if (x2apic_mode)
        return (uint32_t)cpu_rdmsr(0x800u + offset / 16u);
    return lapic[offset / sizeof(uint32_t)];
}

static void apic_write(uint32_t offset, uint32_t value) {
    if (x2apic_mode)
        cpu_wrmsr(0x800u + offset / 16u, value);
    else
        lapic[offset / sizeof(uint32_t)] = value;
}

int apic_init(const acpi_info_t *acpi) {
    if (!acpi || !acpi->local_apic_address) return 0;

    uint64_t apic_base = cpu_rdmsr(IA32_APIC_BASE_MSR);
    x2apic_mode = (apic_base & APIC_X2_MODE) != 0;
    apic_base |= APIC_ENABLE;
    cpu_wrmsr(IA32_APIC_BASE_MSR, apic_base);
    lapic = (volatile uint32_t *)(uintptr_t)acpi->local_apic_address;
    timer_ticks = 0;

    apic_write(APIC_TPR, 0);
    apic_write(APIC_SVR, 0x100u | ARGUS_APIC_SPURIOUS_VECTOR);
    apic_write(APIC_DIVIDE_CONFIG, 0x3u); /* Divide the timer clock by 16. */
    apic_write(APIC_LVT_TIMER, 0x20000u | ARGUS_APIC_TIMER_VECTOR);
    apic_write(APIC_INITIAL_COUNT, 1000000u);

    return apic_read(APIC_SVR) & 0x100u;
}

void apic_eoi(void) { apic_write(APIC_EOI, 0); }

void apic_timer_interrupt(void) {
    ++timer_ticks;
    apic_eoi();
}

uint64_t apic_timer_ticks(void) { return timer_ticks; }
