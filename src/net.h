#ifndef ARGUS_NET_H
#define ARGUS_NET_H

#include "net_abi.h"
#include "pci.h"
#include <stdint.h>

typedef struct {
    uint32_t device_count;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t present;
    uint8_t virtio_modern;
    uint8_t quarantined;
    uint8_t dma_enabled;
    uint8_t egress_enabled;
    uint8_t owner_role;
    uint8_t reserved[3];
    uint32_t core_state_size;
    uint32_t max_frame;
    uint32_t queue_capacity;
} net_info_t;

int net_init(const pci_info_t *pci);
int net_self_test(void);
const net_info_t *net_info(void);
const char *net_device_name(void);
const char *net_core_name(void);
int net_egress_allowed(void);
int net_foundation_online(void);

#endif
