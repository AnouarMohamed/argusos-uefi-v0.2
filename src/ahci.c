#include "ahci.h"
#include "pmm.h"

#define AHCI_HBA_CAP 0x00u
#define AHCI_HBA_GHC 0x04u
#define AHCI_HBA_PI 0x0Cu
#define AHCI_HBA_VS 0x10u
#define AHCI_HBA_CAP2 0x24u
#define AHCI_HBA_BOHC 0x28u
#define AHCI_PORT_BASE 0x100u
#define AHCI_PORT_SIZE 0x80u

#define AHCI_PORT_CLB 0x00u
#define AHCI_PORT_CLBU 0x04u
#define AHCI_PORT_FB 0x08u
#define AHCI_PORT_FBU 0x0Cu
#define AHCI_PORT_IS 0x10u
#define AHCI_PORT_IE 0x14u
#define AHCI_PORT_CMD 0x18u
#define AHCI_PORT_TFD 0x20u
#define AHCI_PORT_SIG 0x24u
#define AHCI_PORT_SSTS 0x28u
#define AHCI_PORT_SERR 0x30u
#define AHCI_PORT_SACT 0x34u
#define AHCI_PORT_CI 0x38u

#define AHCI_GHC_IE (1u << 1)
#define AHCI_GHC_AE (1u << 31)
#define AHCI_CAP_S64A (1u << 31)
#define AHCI_CAP2_BOH (1u << 0)
#define AHCI_BOHC_BOS (1u << 0)
#define AHCI_BOHC_OOS (1u << 1)
#define AHCI_BOHC_BB (1u << 4)

#define AHCI_PORT_CMD_ST (1u << 0)
#define AHCI_PORT_CMD_FRE (1u << 4)
#define AHCI_PORT_CMD_FR (1u << 14)
#define AHCI_PORT_CMD_CR (1u << 15)
#define AHCI_PORT_TFD_DRQ (1u << 3)
#define AHCI_PORT_TFD_BSY (1u << 7)
#define AHCI_PORT_IS_TFES (1u << 30)
#define AHCI_SATA_SIGNATURE 0x00000101u

#define ATA_IDENTIFY_DEVICE 0xECu
#define ATA_READ_DMA_EXT 0x25u
#define FIS_TYPE_REG_H2D 0x27u

#define AHCI_COMMAND_LIST_OFFSET 0u
#define AHCI_RECEIVED_FIS_OFFSET 1024u
#define AHCI_COMMAND_TABLE_OFFSET 1280u
#define AHCI_PRDT_OFFSET 128u
#define AHCI_DMA_PAGES 2u
#define AHCI_DATA_PAGE_OFFSET ARGUS_PAGE_SIZE
#define AHCI_TIMEOUT 10000000u
#define AHCI_MAX_READ_SECTORS 128u

typedef struct {
    volatile uint8_t *abar;
    volatile uint8_t *port;
    uint8_t *command_memory;
    uint8_t *data_buffer;
    uint64_t command_physical;
    uint64_t data_physical;
    uint64_t sector_count;
    uint8_t port_number;
    uint8_t supports_64bit_dma;
    uint8_t online;
    uint8_t reserved;
} ahci_state_t;

static ahci_state_t controller;
static ahci_info_t published_info;
static argus_block_device_v1_t block_device;

static uint32_t mmio_read(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static void mmio_write(
    volatile uint8_t *base,
    uint32_t offset,
    uint32_t value
) {
    *(volatile uint32_t *)(base + offset) = value;
}

static void zero_bytes(uint8_t *bytes, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) bytes[i] = 0;
}

static void copy_bytes(uint8_t *output, const uint8_t *input, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) output[i] = input[i];
}

static uint16_t read_u16(const uint8_t *bytes, uint32_t word) {
    uint32_t offset = word * 2u;
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1u] << 8));
}

static int wait_clear(
    volatile uint8_t *base,
    uint32_t offset,
    uint32_t mask
) {
    for (uint32_t attempt = 0; attempt < AHCI_TIMEOUT; ++attempt)
        if (!(mmio_read(base, offset) & mask)) return 1;
    return 0;
}

static int claim_controller(volatile uint8_t *abar) {
    if (!(mmio_read(abar, AHCI_HBA_CAP2) & AHCI_CAP2_BOH)) return 1;
    uint32_t ownership = mmio_read(abar, AHCI_HBA_BOHC);
    mmio_write(abar, AHCI_HBA_BOHC, ownership | AHCI_BOHC_OOS);
    return wait_clear(abar, AHCI_HBA_BOHC, AHCI_BOHC_BOS | AHCI_BOHC_BB);
}

static int stop_port(volatile uint8_t *port) {
    uint32_t command = mmio_read(port, AHCI_PORT_CMD);
    command &= ~AHCI_PORT_CMD_ST;
    mmio_write(port, AHCI_PORT_CMD, command);
    if (!wait_clear(port, AHCI_PORT_CMD, AHCI_PORT_CMD_CR)) return 0;
    command = mmio_read(port, AHCI_PORT_CMD) & ~AHCI_PORT_CMD_FRE;
    mmio_write(port, AHCI_PORT_CMD, command);
    return wait_clear(port, AHCI_PORT_CMD, AHCI_PORT_CMD_FR);
}

static void start_port(volatile uint8_t *port) {
    uint32_t command = mmio_read(port, AHCI_PORT_CMD);
    command |= AHCI_PORT_CMD_FRE;
    mmio_write(port, AHCI_PORT_CMD, command);
    command |= AHCI_PORT_CMD_ST;
    mmio_write(port, AHCI_PORT_CMD, command);
}

static int sata_port_present(volatile uint8_t *port) {
    uint32_t status = mmio_read(port, AHCI_PORT_SSTS);
    uint32_t detection = status & 0x0Fu;
    uint32_t power = (status >> 8) & 0x0Fu;
    return detection == 3u && power == 1u &&
           mmio_read(port, AHCI_PORT_SIG) == AHCI_SATA_SIGNATURE;
}

static int issue_command(uint8_t command, uint64_t lba, uint16_t sector_count) {
    if (!controller.online && command != ATA_IDENTIFY_DEVICE) return 0;
    volatile uint8_t *port = controller.port;
    if (!wait_clear(port, AHCI_PORT_TFD, AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ))
        return 0;
    if (mmio_read(port, AHCI_PORT_SACT) & 1u ||
        mmio_read(port, AHCI_PORT_CI) & 1u)
        return 0;

    uint32_t *header = (uint32_t *)(controller.command_memory +
                                    AHCI_COMMAND_LIST_OFFSET);
    uint8_t *table = controller.command_memory + AHCI_COMMAND_TABLE_OFFSET;
    zero_bytes((uint8_t *)header, 32u);
    zero_bytes(table, 256u);

    header[0] = 5u | (1u << 16);
    uint64_t table_physical = controller.command_physical +
                              AHCI_COMMAND_TABLE_OFFSET;
    header[2] = (uint32_t)table_physical;
    header[3] = (uint32_t)(table_physical >> 32);

    table[0] = FIS_TYPE_REG_H2D;
    table[1] = 0x80u;
    table[2] = command;
    if (command == ATA_READ_DMA_EXT) {
        table[4] = (uint8_t)lba;
        table[5] = (uint8_t)(lba >> 8);
        table[6] = (uint8_t)(lba >> 16);
        table[7] = 0x40u;
        table[8] = (uint8_t)(lba >> 24);
        table[9] = (uint8_t)(lba >> 32);
        table[10] = (uint8_t)(lba >> 40);
        table[12] = (uint8_t)sector_count;
        table[13] = (uint8_t)(sector_count >> 8);
    }

    uint32_t *prdt = (uint32_t *)(table + AHCI_PRDT_OFFSET);
    prdt[0] = (uint32_t)controller.data_physical;
    prdt[1] = (uint32_t)(controller.data_physical >> 32);
    prdt[2] = 0;
    prdt[3] = 511u;

    mmio_write(port, AHCI_PORT_IS, UINT32_MAX);
    mmio_write(port, AHCI_PORT_SERR, UINT32_MAX);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    mmio_write(port, AHCI_PORT_CI, 1u);

    for (uint32_t attempt = 0; attempt < AHCI_TIMEOUT; ++attempt) {
        uint32_t interrupt_status = mmio_read(port, AHCI_PORT_IS);
        if (interrupt_status & AHCI_PORT_IS_TFES) return 0;
        if (!(mmio_read(port, AHCI_PORT_CI) & 1u)) {
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            return !(mmio_read(port, AHCI_PORT_TFD) & 1u);
        }
    }
    return 0;
}

static uint64_t identify_sector_count(void) {
    zero_bytes(controller.data_buffer, 512u);
    if (!issue_command(ATA_IDENTIFY_DEVICE, 0, 0)) return 0;

    uint16_t capabilities = read_u16(controller.data_buffer, 83u);
    if ((capabilities & 0xC400u) != 0x4400u) return 0;
    uint64_t sectors =
        (uint64_t)read_u16(controller.data_buffer, 100u) |
        ((uint64_t)read_u16(controller.data_buffer, 101u) << 16) |
        ((uint64_t)read_u16(controller.data_buffer, 102u) << 32) |
        ((uint64_t)read_u16(controller.data_buffer, 103u) << 48);
    if (!sectors || sectors > (1ULL << 48)) return 0;

    uint16_t sector_size_info = read_u16(controller.data_buffer, 106u);
    if ((sector_size_info & 0xC000u) == 0x4000u &&
        (sector_size_info & (1u << 12))) {
        uint32_t logical_words =
            (uint32_t)read_u16(controller.data_buffer, 117u) |
            ((uint32_t)read_u16(controller.data_buffer, 118u) << 16);
        if (logical_words != 256u) return 0;
    }
    return sectors;
}

static int32_t ahci_read(
    const void *context,
    uint64_t first_sector,
    uint32_t sector_count,
    uint8_t *output,
    uint64_t output_capacity
) {
    if (context != &controller || !sector_count || !output ||
        sector_count > AHCI_MAX_READ_SECTORS)
        return ARGUS_BLOCK_INVALID;
    if (!controller.online) return ARGUS_BLOCK_IO_ERROR;
    if (first_sector >= controller.sector_count ||
        sector_count > controller.sector_count - first_sector)
        return ARGUS_BLOCK_RANGE;
    uint64_t required = (uint64_t)sector_count * 512u;
    if (output_capacity < required) return ARGUS_BLOCK_BUFFER_TOO_SMALL;

    for (uint32_t index = 0; index < sector_count; ++index) {
        if (!issue_command(ATA_READ_DMA_EXT, first_sector + index, 1u)) {
            controller.online = 0;
            stop_port(controller.port);
            return ARGUS_BLOCK_IO_ERROR;
        }
        copy_bytes(output + (uint64_t)index * 512u,
                   controller.data_buffer, 512u);
    }
    return ARGUS_BLOCK_OK;
}

static void release_dma(void) {
    if (controller.command_physical)
        pmm_release_pages(controller.command_physical, AHCI_DMA_PAGES);
    controller = (ahci_state_t){0};
}

int ahci_init(
    const pci_device_t *pci_device,
    const paging_info_t *paging_info
) {
    controller = (ahci_state_t){0};
    published_info = (ahci_info_t){0};
    block_device = (argus_block_device_v1_t){0};
    if (!pci_device || !paging_info || pci_device->class_code != 0x01u ||
        pci_device->subclass != 0x06u ||
        pci_device->programming_interface != 0x01u)
        return 0;

    uint32_t bar = pci_config_read32(pci_device, 0x24u);
    if (bar == UINT32_MAX || (bar & 1u) || (bar & 6u) == 4u) return 0;
    uint64_t abar_address = (uint64_t)(bar & 0xFFFFFFF0u);
    if (!abar_address || abar_address >= paging_info->mapped_bytes ||
        paging_info->mapped_bytes - abar_address <
            AHCI_PORT_BASE + 32u * AHCI_PORT_SIZE ||
        !paging_mark_mmio(
            paging_info,
            abar_address,
            AHCI_PORT_BASE + 32u * AHCI_PORT_SIZE
        ))
        return 0;
    if (!pci_enable_memory_bus_master(pci_device)) return 0;

    volatile uint8_t *abar = (volatile uint8_t *)(uintptr_t)abar_address;
    if (!claim_controller(abar)) return 0;
    uint32_t global_control = mmio_read(abar, AHCI_HBA_GHC);
    global_control = (global_control | AHCI_GHC_AE) & ~AHCI_GHC_IE;
    mmio_write(abar, AHCI_HBA_GHC, global_control);

    uint32_t implemented = mmio_read(abar, AHCI_HBA_PI);
    uint32_t port_number = 32u;
    volatile uint8_t *port = 0;
    for (uint32_t candidate = 0; candidate < 32u; ++candidate) {
        if (!(implemented & (1u << candidate))) continue;
        volatile uint8_t *candidate_port =
            abar + AHCI_PORT_BASE + candidate * AHCI_PORT_SIZE;
        if (sata_port_present(candidate_port)) {
            port_number = candidate;
            port = candidate_port;
            break;
        }
    }
    if (!port || port_number >= 32u || !stop_port(port)) return 0;

    uint64_t dma = pmm_alloc_pages(AHCI_DMA_PAGES);
    if (!dma) return 0;
    int supports_64bit = (mmio_read(abar, AHCI_HBA_CAP) & AHCI_CAP_S64A) != 0;
    if (!supports_64bit && dma > UINT32_MAX - AHCI_DMA_PAGES * ARGUS_PAGE_SIZE) {
        pmm_release_pages(dma, AHCI_DMA_PAGES);
        return 0;
    }
    zero_bytes((uint8_t *)(uintptr_t)dma,
               AHCI_DMA_PAGES * ARGUS_PAGE_SIZE);

    controller.abar = abar;
    controller.port = port;
    controller.command_memory = (uint8_t *)(uintptr_t)dma;
    controller.data_buffer =
        (uint8_t *)(uintptr_t)(dma + AHCI_DATA_PAGE_OFFSET);
    controller.command_physical = dma;
    controller.data_physical = dma + AHCI_DATA_PAGE_OFFSET;
    controller.port_number = (uint8_t)port_number;
    controller.supports_64bit_dma = (uint8_t)supports_64bit;

    mmio_write(port, AHCI_PORT_CLB, (uint32_t)dma);
    mmio_write(port, AHCI_PORT_CLBU, (uint32_t)(dma >> 32));
    uint64_t fis = dma + AHCI_RECEIVED_FIS_OFFSET;
    mmio_write(port, AHCI_PORT_FB, (uint32_t)fis);
    mmio_write(port, AHCI_PORT_FBU, (uint32_t)(fis >> 32));
    mmio_write(port, AHCI_PORT_IE, 0);
    mmio_write(port, AHCI_PORT_IS, UINT32_MAX);
    mmio_write(port, AHCI_PORT_SERR, UINT32_MAX);
    start_port(port);

    uint64_t sectors = identify_sector_count();
    if (!sectors) {
        stop_port(port);
        release_dma();
        return 0;
    }
    controller.sector_count = sectors;
    controller.online = 1;

    block_device.abi_version = ARGUS_BLOCK_ABI_VERSION;
    block_device.struct_size = (uint32_t)sizeof(block_device);
    block_device.name[0] = 'a';
    block_device.name[1] = 'h';
    block_device.name[2] = 'c';
    block_device.name[3] = 'i';
    block_device.name[4] = '0';
    block_device.sector_size = 512u;
    block_device.sector_count = sectors;
    block_device.context = &controller;
    block_device.read = ahci_read;

    published_info.abar = abar_address;
    published_info.sector_count = sectors;
    published_info.version = mmio_read(abar, AHCI_HBA_VS);
    published_info.implemented_ports = implemented;
    published_info.port = (uint8_t)port_number;
    published_info.supports_64bit_dma = (uint8_t)supports_64bit;
    published_info.initialized = 1;
    return block_device_valid(&block_device);
}

int ahci_self_test(void) {
    if (!controller.online || !block_device_valid(&block_device)) return 0;
    uint8_t sector[512];
    return block_read(&block_device, 0, 1u, sector, sizeof(sector)) ==
               ARGUS_BLOCK_OK &&
           sector[510] == 0x55u && sector[511] == 0xAAu;
}

const ahci_info_t *ahci_info(void) {
    return published_info.initialized && controller.online ? &published_info : 0;
}

const argus_block_device_v1_t *ahci_block_device(void) {
    return controller.online && block_device_valid(&block_device)
        ? &block_device : 0;
}
