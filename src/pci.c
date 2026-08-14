#include "pci.h"

#define PCI_CONFIG_ADDRESS 0x0CF8u
#define PCI_CONFIG_DATA 0x0CFCu
#define PCI_ENABLE_BIT 0x80000000u
#define PCI_INVALID_VENDOR 0xFFFFu
#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_SATA 0x06u
#define PCI_PROGIF_AHCI 0x01u
#define PCI_CLASS_NETWORK 0x02u
#define PCI_COMMAND_IO_SPACE (1u << 0)
#define PCI_COMMAND_MEMORY_SPACE (1u << 1)
#define PCI_COMMAND_BUS_MASTER (1u << 2)
#define PCI_COMMAND_INTERRUPT_DISABLE (1u << 10)
#define PCI_STATUS_CAPABILITIES (1u << 4)
#define PCI_CAPABILITY_POINTER 0x34u
#define PCI_CAPABILITY_MSI 0x05u
#define PCI_CAPABILITY_MSIX 0x11u
#define PCI_MSI_ENABLE (1u << 0)
#define PCI_MSIX_FUNCTION_MASK (1u << 14)
#define PCI_MSIX_ENABLE (1u << 15)

extern void cpu_out32(uint16_t port, uint32_t value);
extern void cpu_out16(uint16_t port, uint16_t value);
extern uint32_t cpu_in32(uint16_t port);

static pci_info_t discovered;

static uint32_t config_address(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
) {
    return PCI_ENABLE_BIT | ((uint32_t)bus << 16) |
           ((uint32_t)device << 11) | ((uint32_t)function << 8) |
           (offset & 0xFCu);
}

static uint32_t read_location(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
) {
    cpu_out32(PCI_CONFIG_ADDRESS,
              config_address(bus, device, function, offset));
    return cpu_in32(PCI_CONFIG_DATA);
}

uint32_t pci_config_read32(const pci_device_t *device, uint8_t offset) {
    if (!device || (offset & 3u)) return UINT32_MAX;
    return read_location(
        device->bus,
        device->device,
        device->function,
        offset
    );
}

static uint8_t config_read8(const pci_device_t *device, uint8_t offset) {
    uint32_t value = pci_config_read32(device, offset & 0xFCu);
    return value == UINT32_MAX
        ? UINT8_MAX : (uint8_t)(value >> ((offset & 3u) * 8u));
}

static uint16_t config_read16(const pci_device_t *device, uint8_t offset) {
    if (offset & 1u) return UINT16_MAX;
    uint32_t value = pci_config_read32(device, offset & 0xFCu);
    return value == UINT32_MAX
        ? UINT16_MAX : (uint16_t)(value >> ((offset & 2u) * 8u));
}

static int config_write16(
    const pci_device_t *device,
    uint8_t offset,
    uint16_t value
) {
    if (!device || (offset & 1u)) return 0;
    cpu_out32(
        PCI_CONFIG_ADDRESS,
        config_address(device->bus, device->device, device->function, offset)
    );
    cpu_out16((uint16_t)(PCI_CONFIG_DATA + (offset & 2u)), value);
    return config_read16(device, offset) == value;
}

static int message_interrupt_state(
    const pci_device_t *device,
    int disable
) {
    uint32_t command_status = pci_config_read32(device, 0x04u);
    if (command_status == UINT32_MAX) return 0;
    if (!((command_status >> 16) & PCI_STATUS_CAPABILITIES)) return 1;
    uint8_t capability = config_read8(device, PCI_CAPABILITY_POINTER);
    if (capability & 3u) return 0;
    uint64_t visited = 0;
    for (uint32_t count = 0; capability && count < 48u; ++count) {
        if (capability < 0x40u || capability > 0xFCu) return 0;
        uint32_t slot = capability >> 2;
        uint64_t bit = 1ULL << slot;
        if (visited & bit) return 0;
        visited |= bit;
        uint8_t identifier = config_read8(device, capability);
        uint8_t next = config_read8(device, capability + 1u);
        if (identifier == UINT8_MAX || next == UINT8_MAX || (next & 3u))
            return 0;
        if (identifier == PCI_CAPABILITY_MSI) {
            uint16_t control = config_read16(device, capability + 2u);
            if (control == UINT16_MAX) return 0;
            if (disable) {
                control &= (uint16_t)~PCI_MSI_ENABLE;
                if (!config_write16(device, capability + 2u, control)) return 0;
            } else if (control & PCI_MSI_ENABLE) {
                return 0;
            }
        } else if (identifier == PCI_CAPABILITY_MSIX) {
            uint16_t control = config_read16(device, capability + 2u);
            if (control == UINT16_MAX) return 0;
            if (disable) {
                control &= (uint16_t)~PCI_MSIX_ENABLE;
                control |= PCI_MSIX_FUNCTION_MASK;
                if (!config_write16(device, capability + 2u, control)) return 0;
            } else if ((control & (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) !=
                       PCI_MSIX_FUNCTION_MASK) {
                return 0;
            }
        }
        capability = next;
    }
    return capability == 0;
}

static void record_function(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint32_t identity
) {
    pci_device_t found;
    uint32_t class_data = read_location(bus, device, function, 0x08u);
    uint32_t header_data = read_location(bus, device, function, 0x0Cu);
    found.bus = bus;
    found.device = device;
    found.function = function;
    found.class_code = (uint8_t)(class_data >> 24);
    found.subclass = (uint8_t)(class_data >> 16);
    found.programming_interface = (uint8_t)(class_data >> 8);
    found.header_type = (uint8_t)(header_data >> 16);
    found.reserved = 0;
    found.vendor_id = (uint16_t)identity;
    found.device_id = (uint16_t)(identity >> 16);
    ++discovered.device_count;

    if (found.class_code == PCI_CLASS_MASS_STORAGE &&
        found.subclass == PCI_SUBCLASS_SATA &&
        found.programming_interface == PCI_PROGIF_AHCI) {
        if (!discovered.ahci_count) discovered.first_ahci = found;
        ++discovered.ahci_count;
    }
    if (found.class_code == PCI_CLASS_NETWORK) {
        if (!discovered.network_count) discovered.first_network = found;
        ++discovered.network_count;
        if (pci_quarantine_device(&found))
            ++discovered.network_quarantined_count;
    }
}

int pci_init(void) {
    discovered = (pci_info_t){0};
    for (uint32_t bus = 0; bus < 256u; ++bus) {
        for (uint32_t device = 0; device < 32u; ++device) {
            uint32_t identity = read_location(
                (uint8_t)bus,
                (uint8_t)device,
                0,
                0
            );
            if (!(uint16_t)identity ||
                (uint16_t)identity == PCI_INVALID_VENDOR)
                continue;
            record_function((uint8_t)bus, (uint8_t)device, 0, identity);

            uint32_t header = read_location(
                (uint8_t)bus,
                (uint8_t)device,
                0,
                0x0Cu
            );
            if (!((header >> 23) & 1u)) continue;
            for (uint32_t function = 1; function < 8u; ++function) {
                identity = read_location(
                    (uint8_t)bus,
                    (uint8_t)device,
                    (uint8_t)function,
                    0
                );
                if ((uint16_t)identity &&
                    (uint16_t)identity != PCI_INVALID_VENDOR)
                    record_function(
                        (uint8_t)bus,
                        (uint8_t)device,
                        (uint8_t)function,
                        identity
                    );
            }
        }
    }
    return discovered.device_count != 0;
}

int pci_self_test(void) {
    if (!discovered.device_count) return 0;
    if (discovered.ahci_count) {
        const pci_device_t *device = &discovered.first_ahci;
        uint32_t identity = pci_config_read32(device, 0);
        uint32_t class_data = pci_config_read32(device, 0x08u);
        if ((uint16_t)identity != device->vendor_id ||
            (uint16_t)(identity >> 16) != device->device_id ||
            (uint8_t)(class_data >> 24) != PCI_CLASS_MASS_STORAGE ||
            (uint8_t)(class_data >> 16) != PCI_SUBCLASS_SATA ||
            (uint8_t)(class_data >> 8) != PCI_PROGIF_AHCI)
            return 0;
    }
    if (discovered.network_count) {
        if (discovered.network_quarantined_count != discovered.network_count)
            return 0;
        const pci_device_t *device = &discovered.first_network;
        uint32_t identity = pci_config_read32(device, 0);
        uint32_t class_data = pci_config_read32(device, 0x08u);
        if ((uint16_t)identity != device->vendor_id ||
            (uint16_t)(identity >> 16) != device->device_id ||
            (uint8_t)(class_data >> 24) != PCI_CLASS_NETWORK)
            return 0;
    }
    return 1;
}

const pci_info_t *pci_info(void) {
    return discovered.device_count ? &discovered : 0;
}

int pci_enable_memory_bus_master(const pci_device_t *device) {
    if (!device) return 0;
    uint32_t command_status = pci_config_read32(device, 0x04u);
    if (command_status == UINT32_MAX) return 0;
    uint32_t command = command_status & 0xFFFFu;
    command |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    cpu_out32(
        PCI_CONFIG_ADDRESS,
        config_address(device->bus, device->device, device->function, 0x04u)
    );
    cpu_out16(PCI_CONFIG_DATA, (uint16_t)command);
    uint32_t verified = pci_config_read32(device, 0x04u);
    return (verified & (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) ==
           (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);
}

uint16_t pci_device_command(const pci_device_t *device) {
    uint32_t command_status = pci_config_read32(device, 0x04u);
    return command_status == UINT32_MAX
        ? UINT16_MAX : (uint16_t)command_status;
}

int pci_quarantine_device(const pci_device_t *device) {
    uint16_t command = pci_device_command(device);
    if (command == UINT16_MAX) return 0;
    command &= (uint16_t)~(
        PCI_COMMAND_IO_SPACE |
        PCI_COMMAND_MEMORY_SPACE |
        PCI_COMMAND_BUS_MASTER
    );
    command |= PCI_COMMAND_INTERRUPT_DISABLE;
    cpu_out32(
        PCI_CONFIG_ADDRESS,
        config_address(device->bus, device->device, device->function, 0x04u)
    );
    cpu_out16(PCI_CONFIG_DATA, command);
    if (!message_interrupt_state(device, 1)) return 0;
    return pci_device_is_quarantined(device);
}

int pci_device_is_quarantined(const pci_device_t *device) {
    uint16_t command = pci_device_command(device);
    return command != UINT16_MAX &&
        !(command & (PCI_COMMAND_IO_SPACE |
                     PCI_COMMAND_MEMORY_SPACE |
                     PCI_COMMAND_BUS_MASTER)) &&
        (command & PCI_COMMAND_INTERRUPT_DISABLE) &&
        message_interrupt_state(device, 0);
}
