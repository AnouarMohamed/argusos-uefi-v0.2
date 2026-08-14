#include "net.h"
#include "anonymity.h"
#include "capability.h"
#include "module.h"

#define NET_STATE_STORAGE_BYTES 32768u
#define NET_AUTHORITY_PRINCIPAL 0x4E45542D4B45524EULL
#define PCI_STATUS_CAPABILITIES (1u << 4)
#define PCI_CAPABILITY_POINTER 0x34u
#define PCI_CAPABILITY_VENDOR 0x09u
#define VIRTIO_VENDOR_ID 0x1AF4u
#define VIRTIO_NET_MODERN_DEVICE_ID 0x1041u
#define VIRTIO_PCI_CAP_COMMON_CFG 1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2u
#define VIRTIO_PCI_CAP_ISR_CFG 3u
#define VIRTIO_PCI_CAP_DEVICE_CFG 4u
#define VIRTIO_COMMON_MIN_BYTES 56u
#define VIRTIO_CAP_MIN_BYTES 16u
#define VIRTIO_NOTIFY_CAP_MIN_BYTES 20u

static const argus_net_v1_t *core;
static net_info_t published;
static pci_device_t owned_device;
static argus_capability_table_t authority;
static uint64_t authority_handle;
static uint64_t authority_object;
static int validated;
static uint8_t state_storage[NET_STATE_STORAGE_BYTES]
    __attribute__((aligned(16)));

static uint8_t config_read8(const pci_device_t *device, uint8_t offset) {
    uint32_t value = pci_config_read32(device, offset & 0xFCu);
    if (value == UINT32_MAX) return UINT8_MAX;
    return (uint8_t)(value >> ((offset & 3u) * 8u));
}

static int valid_memory_bar(const pci_device_t *device, uint8_t bar) {
    if (bar >= 6u) return 0;
    uint8_t offset = (uint8_t)(0x10u + bar * 4u);
    uint32_t value = pci_config_read32(device, offset);
    if (value == UINT32_MAX || (value & 1u)) return 0;
    if ((value & 6u) == 4u && bar == 5u) return 0;
    return 1;
}

static int capability_region_valid(
    const pci_device_t *device,
    uint8_t capability,
    uint8_t capability_length,
    uint8_t type
) {
    if (capability > 0xF0u || capability_length < VIRTIO_CAP_MIN_BYTES)
        return 0;
    uint32_t bar_data = pci_config_read32(device, capability + 4u);
    uint32_t offset = pci_config_read32(device, capability + 8u);
    uint32_t length = pci_config_read32(device, capability + 12u);
    if (bar_data == UINT32_MAX || offset == UINT32_MAX ||
        length == UINT32_MAX || !length || offset > UINT32_MAX - length ||
        !valid_memory_bar(device, (uint8_t)bar_data))
        return 0;
    if (type == VIRTIO_PCI_CAP_COMMON_CFG)
        return length >= VIRTIO_COMMON_MIN_BYTES;
    if (type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
        if (capability > 0xECu ||
            capability_length < VIRTIO_NOTIFY_CAP_MIN_BYTES || length < 2u)
            return 0;
        uint32_t multiplier = pci_config_read32(device, capability + 16u);
        return multiplier != UINT32_MAX &&
            (!multiplier || (!(multiplier & 1u) &&
                              !(multiplier & (multiplier - 1u))));
    }
    if (type == VIRTIO_PCI_CAP_ISR_CFG) return length >= 1u;
    if (type == VIRTIO_PCI_CAP_DEVICE_CFG) return 1;
    return 0;
}

static int virtio_modern_transport_valid(const pci_device_t *device) {
    if (!device || device->vendor_id != VIRTIO_VENDOR_ID ||
        device->device_id != VIRTIO_NET_MODERN_DEVICE_ID ||
        device->class_code != 0x02u)
        return 0;
    uint32_t command_status = pci_config_read32(device, 0x04u);
    if (command_status == UINT32_MAX ||
        !((command_status >> 16) & PCI_STATUS_CAPABILITIES))
        return 0;
    uint8_t capability = config_read8(device, PCI_CAPABILITY_POINTER);
    if (capability & 3u) return 0;
    uint64_t visited = 0;
    uint32_t found = 0;
    for (uint32_t count = 0; capability && count < 48u; ++count) {
        if (capability < 0x40u || capability > 0xFCu) return 0;
        uint32_t slot = capability >> 2;
        uint64_t bit = 1ULL << slot;
        if (visited & bit) return 0;
        visited |= bit;
        uint32_t header = pci_config_read32(device, capability);
        if (header == UINT32_MAX) return 0;
        uint8_t identifier = (uint8_t)header;
        uint8_t next = (uint8_t)(header >> 8);
        if (next & 3u) return 0;
        uint8_t length = (uint8_t)(header >> 16);
        uint8_t type = (uint8_t)(header >> 24);
        if (identifier == PCI_CAPABILITY_VENDOR &&
            type >= VIRTIO_PCI_CAP_COMMON_CFG &&
            type <= VIRTIO_PCI_CAP_DEVICE_CFG) {
            if (!capability_region_valid(device, capability, length, type))
                return 0;
            found |= 1u << type;
        }
        capability = next;
    }
    if (capability) return 0;
    return (found & ((1u << VIRTIO_PCI_CAP_COMMON_CFG) |
                     (1u << VIRTIO_PCI_CAP_NOTIFY_CFG) |
                     (1u << VIRTIO_PCI_CAP_ISR_CFG))) ==
        ((1u << VIRTIO_PCI_CAP_COMMON_CFG) |
         (1u << VIRTIO_PCI_CAP_NOTIFY_CFG) |
         (1u << VIRTIO_PCI_CAP_ISR_CFG));
}

static uint64_t device_object(const pci_device_t *device) {
    return ((uint64_t)device->bus << 32) |
        ((uint64_t)device->device << 16) |
        ((uint64_t)device->function << 8) | 1u;
}

int net_init(const pci_info_t *pci) {
    core = module_net_descriptor();
    published = (net_info_t){0};
    owned_device = (pci_device_t){0};
    authority_handle = 0;
    authority_object = 0;
    validated = 0;
    capability_table_init(&authority, NET_AUTHORITY_PRINCIPAL);
    if (!core || core->state_size > sizeof(state_storage) ||
        core->state_alignment > 16u ||
        core->initialize(state_storage, sizeof(state_storage)) != ARGUS_NET_OK)
        return 0;
    published.core_state_size = core->state_size;
    published.max_frame = core->max_frame;
    published.queue_capacity = core->queue_capacity;
    published.owner_role = ARGUS_SECURITY_ROLE_TOR_TRANSPORT;
    if (!pci) return 0;
    published.device_count = pci->network_count;
    if (!pci->network_count) return 1;
    if (pci->network_quarantined_count != pci->network_count) return 0;

    owned_device = pci->first_network;
    published.present = 1;
    published.vendor_id = owned_device.vendor_id;
    published.device_id = owned_device.device_id;
    published.bus = owned_device.bus;
    published.device = owned_device.device;
    published.function = owned_device.function;
    published.quarantined = (uint8_t)pci_quarantine_device(&owned_device);
    if (!published.quarantined) return 0;
    published.virtio_modern = (uint8_t)virtio_modern_transport_valid(
        &owned_device
    );
    if (!published.virtio_modern) return 1;
    if (!anonymity_capability_allowed(
            ARGUS_SECURITY_ROLE_TOR_TRANSPORT,
            ARGUS_CAPABILITY_RAW_NETWORK,
            ARGUS_CAP_RIGHT_CONNECT))
        return 0;
    authority_object = device_object(&owned_device);
    authority_handle = capability_grant(
        &authority,
        ARGUS_CAPABILITY_RAW_NETWORK,
        ARGUS_CAP_RIGHT_CONNECT,
        authority_object
    );
    return authority_handle != 0;
}

static int run_self_test(void) {
    if (!core || core->self_test() != 1 ||
        core->initialize(state_storage, sizeof(state_storage)) != ARGUS_NET_OK ||
        core->inspect(0, 0, 0) != ARGUS_NET_INVALID ||
        net_egress_allowed() || published.dma_enabled ||
        published.egress_enabled)
        return 0;
    uint8_t output[1];
    uint64_t output_length = 0;
    if (core->dequeue(
            state_storage,
            ARGUS_NET_QUEUE_INGRESS,
            output,
            sizeof(output),
            &output_length) != ARGUS_NET_QUEUE_EMPTY)
        return 0;
    uint32_t next = UINT32_MAX;
    if (core->tcp_transition(
            ARGUS_TCP_STATE_CLOSED,
            ARGUS_TCP_EVENT_ACTIVE_OPEN,
            &next) != ARGUS_NET_OK || next != ARGUS_TCP_STATE_SYN_SENT ||
        core->tcp_transition(
            ARGUS_TCP_STATE_CLOSED,
            ARGUS_TCP_EVENT_DATA,
            &next) != ARGUS_NET_STATE_ERROR)
        return 0;
    if (!published.present)
        return !published.device_count && !authority_handle;
    if (!published.quarantined || !pci_device_is_quarantined(&owned_device))
        return 0;
    if (!published.virtio_modern) return !authority_handle;
    uint64_t resolved = 0;
    return authority_handle &&
        capability_count_type(&authority, ARGUS_CAPABILITY_RAW_NETWORK) == 1u &&
        capability_resolve(
            &authority,
            authority_handle,
            ARGUS_CAPABILITY_RAW_NETWORK,
            ARGUS_CAP_RIGHT_CONNECT,
            &resolved) &&
        resolved == authority_object;
}

int net_self_test(void) {
    validated = run_self_test();
    return validated;
}

const net_info_t *net_info(void) {
    return core ? &published : 0;
}

const char *net_device_name(void) {
    if (!published.present) return "none";
    return published.virtio_modern ? "virtio-net" : "unsupported";
}

const char *net_core_name(void) {
    return core ? core->name : "offline";
}

int net_egress_allowed(void) { return 0; }

int net_foundation_online(void) {
    return validated && core && !net_egress_allowed() &&
        !published.dma_enabled && !published.egress_enabled &&
        (!published.present || published.quarantined);
}
