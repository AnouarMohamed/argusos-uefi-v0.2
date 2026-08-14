#ifndef ARGUS_PCI_H
#define ARGUS_PCI_H

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t header_type;
    uint8_t reserved;
    uint16_t vendor_id;
    uint16_t device_id;
} pci_device_t;

typedef struct {
    uint32_t device_count;
    uint32_t ahci_count;
    pci_device_t first_ahci;
} pci_info_t;

int pci_init(void);
int pci_self_test(void);
const pci_info_t *pci_info(void);
uint32_t pci_config_read32(const pci_device_t *device, uint8_t offset);
int pci_enable_memory_bus_master(const pci_device_t *device);

#endif
