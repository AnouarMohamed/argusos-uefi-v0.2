#ifndef ARGUS_AHCI_H
#define ARGUS_AHCI_H

#include "block.h"
#include "paging.h"
#include "pci.h"
#include <stdint.h>

typedef struct {
    uint64_t abar;
    uint64_t sector_count;
    uint32_t version;
    uint32_t implemented_ports;
    uint8_t port;
    uint8_t supports_64bit_dma;
    uint8_t initialized;
    uint8_t reserved;
} ahci_info_t;

int ahci_init(
    const pci_device_t *pci_device,
    const paging_info_t *paging_info
);
int ahci_self_test(void);
const ahci_info_t *ahci_info(void);
const argus_block_device_v1_t *ahci_block_device(void);

#endif
