#include "pci.h"

#define PCI_CONFIG_ADDRESS 0x0CF8u
#define PCI_CONFIG_DATA 0x0CFCu
#define PCI_ENABLE_BIT 0x80000000u
#define PCI_INVALID_VENDOR 0xFFFFu
#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_SATA 0x06u
#define PCI_PROGIF_AHCI 0x01u
#define PCI_COMMAND_MEMORY_SPACE (1u << 1)
#define PCI_COMMAND_BUS_MASTER (1u << 2)

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
    if (!discovered.ahci_count) return 1;
    const pci_device_t *device = &discovered.first_ahci;
    uint32_t identity = pci_config_read32(device, 0);
    uint32_t class_data = pci_config_read32(device, 0x08u);
    return (uint16_t)identity == device->vendor_id &&
           (uint16_t)(identity >> 16) == device->device_id &&
           (uint8_t)(class_data >> 24) == PCI_CLASS_MASS_STORAGE &&
           (uint8_t)(class_data >> 16) == PCI_SUBCLASS_SATA &&
           (uint8_t)(class_data >> 8) == PCI_PROGIF_AHCI;
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
