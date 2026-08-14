#include "apic.h"
#include "arch.h"

#define IA32_APIC_BASE_MSR 0x1Bu
#define APIC_ENABLE (1ULL << 11)
#define APIC_X2_MODE (1ULL << 10)

#define APIC_TPR          0x080u
#define APIC_ID           0x020u
#define APIC_EOI          0x0B0u
#define APIC_SVR          0x0F0u
#define APIC_LVT_TIMER    0x320u
#define APIC_INITIAL_COUNT 0x380u
#define APIC_DIVIDE_CONFIG 0x3E0u
#define IOAPIC_REDIRECT_WRITABLE_LOW 0x1AFFFu

extern uint64_t cpu_rdmsr(uint32_t msr);
extern void cpu_wrmsr(uint32_t msr, uint64_t value);

static volatile uint32_t *lapic;
static int x2apic_mode;
static volatile uint64_t timer_ticks;

static void timer_handler(interrupt_frame_t *frame) {
    (void)frame;
    apic_timer_interrupt();
}

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

static uint32_t ioapic_read(volatile uint32_t *ioapic, uint8_t reg) {
    ioapic[0] = reg;
    return ioapic[4];
}

static void ioapic_write(volatile uint32_t *ioapic, uint8_t reg, uint32_t value) {
    ioapic[0] = reg;
    ioapic[4] = value;
}

int apic_init(const acpi_info_t *acpi) {
    if (!acpi || !acpi->local_apic_address) return 0;
    if (!interrupt_register(ARGUS_APIC_TIMER_VECTOR, timer_handler)) return 0;

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

int apic_route_gsi(
    const acpi_info_t *acpi,
    uint32_t gsi,
    uint16_t flags,
    uint8_t vector
) {
    if (!acpi || !acpi->io_apic_address || !lapic || vector < 32u ||
        vector == ARGUS_APIC_SPURIOUS_VECTOR || gsi < acpi->io_apic_gsi_base)
        return 0;

    uint32_t polarity = flags & 3u;
    uint32_t trigger = (flags >> 2) & 3u;
    if (polarity == 2u || trigger == 2u) return 0;

    volatile uint32_t *ioapic =
        (volatile uint32_t *)(uintptr_t)acpi->io_apic_address;
    uint32_t redirection = gsi - acpi->io_apic_gsi_base;
    uint32_t maximum = (ioapic_read(ioapic, 1u) >> 16) & 0xFFu;
    if (redirection > maximum || redirection > 119u) return 0;

    uint32_t apic_id = apic_read(APIC_ID);
    if (!x2apic_mode) apic_id >>= 24;
    if (apic_id > 0xFFu) return 0;

    uint32_t low = vector;
    if (polarity == 3u) low |= 1u << 13;
    if (trigger == 3u) low |= 1u << 15;
    uint8_t low_register = (uint8_t)(0x10u + redirection * 2u);
    ioapic_write(ioapic, low_register, 1u << 16);
    ioapic_write(ioapic, (uint8_t)(low_register + 1u), apic_id << 24);
    ioapic_write(ioapic, low_register, low);
    if ((ioapic_read(ioapic, low_register) & IOAPIC_REDIRECT_WRITABLE_LOW) != low) {
        ioapic_write(ioapic, low_register, low | (1u << 16));
        return 0;
    }
    return 1;
}

void apic_eoi(void) { apic_write(APIC_EOI, 0); }

void apic_timer_interrupt(void) {
    ++timer_ticks;
    apic_eoi();
}

uint64_t apic_timer_ticks(void) { return timer_ticks; }
