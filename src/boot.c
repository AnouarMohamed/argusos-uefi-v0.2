#include "boot.h"
#include "boot_info.h"
#include "console.h"
#include "gop.h"
#include "kernel.h"
#include "serial.h"

#define KERNEL_STACK_PAGES 16u

_Static_assert(sizeof(boot_info_t) <= ARGUS_PAGE_SIZE, "boot_info_t must fit in one page");

static void zero_bytes(void *address, uint64_t size) {
    uint8_t *p = (uint8_t *)address;
    while (size--) *p++ = 0;
}

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3)
        return 0;
    for (unsigned i = 0; i < 8; ++i)
        if (a->Data4[i] != b->Data4[i]) return 0;
    return 1;
}

static VOID *find_acpi_rsdp(EFI_SYSTEM_TABLE *st) {
    EFI_GUID acpi20 = EFI_ACPI_20_TABLE_GUID;
    EFI_GUID acpi10 = EFI_ACPI_TABLE_GUID;
    VOID *legacy = 0;
    for (UINTN i = 0; i < st->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *entry = &st->ConfigurationTable[i];
        if (guid_equal(&entry->VendorGuid, &acpi20)) return entry->VendorTable;
        if (guid_equal(&entry->VendorGuid, &acpi10)) legacy = entry->VendorTable;
    }
    return legacy;
}

static EFI_STATUS allocate_pages(
    EFI_BOOT_SERVICES *bs,
    UINTN pages,
    physical_range_t *range
) {
    EFI_PHYSICAL_ADDRESS address = 0;
    EFI_STATUS status = bs->AllocatePages(
        AllocateAnyPages, EfiLoaderData, pages, &address);
    if (status != EFI_SUCCESS) return status;
    range->base = address;
    range->size = (uint64_t)pages * ARGUS_PAGE_SIZE;
    return EFI_SUCCESS;
}

static int memory_type_needs_tracking(uint32_t type) {
    return (type >= EfiLoaderCode && type <= EfiACPIMemoryNVS &&
            type != EfiUnusableMemory) || type == EfiPersistentMemory;
}

static uint64_t highest_tracked_page(const memory_map_t *map) {
    uint64_t highest = 0;
    unsigned char *p = map->buffer;
    unsigned char *end = p + map->size;
    while (p < end) {
        EFI_MEMORY_DESCRIPTOR *descriptor = (EFI_MEMORY_DESCRIPTOR *)p;
        if (memory_type_needs_tracking(descriptor->Type) &&
            descriptor->NumberOfPages <=
                (UINT64_MAX - descriptor->PhysicalStart) / ARGUS_PAGE_SIZE) {
            uint64_t range_end = descriptor->PhysicalStart +
                                 descriptor->NumberOfPages * ARGUS_PAGE_SIZE;
            uint64_t end_page = range_end / ARGUS_PAGE_SIZE;
            if (end_page > highest) highest = end_page;
        }
        p += map->descriptor_size;
    }
    return highest;
}

static void capture_framebuffer(boot_info_t *info) {
    if (!console_uses_framebuffer()) return;
    const argus_gop_t *gop = gop_info();
    info->framebuffer.base = gop->fb_base;
    info->framebuffer.size = gop->fb_size;
    info->framebuffer.width = gop->width;
    info->framebuffer.height = gop->height;
    info->framebuffer.pitch_pixels = gop->pitch_pixels;
    info->framebuffer.format = gop->format;
    info->framebuffer.masks = gop->masks;
    info->framebuffer.usable = 1;
}

static void free_range(EFI_BOOT_SERVICES *bs, physical_range_t range) {
    if (range.base && range.size)
        bs->FreePages(range.base, (UINTN)(range.size / ARGUS_PAGE_SIZE));
}

static EFI_STATUS prepare_allocations(
    EFI_BOOT_SERVICES *bs,
    uint64_t page_count,
    boot_info_t **info_out
) {
    physical_range_t info_range = {0, 0};
    physical_range_t stack_range = {0, 0};
    physical_range_t bitmap_range = {0, 0};

    uint64_t bytes_per_bitmap = (page_count + 7u) / 8u;
    if (bytes_per_bitmap > UINT64_MAX / 2u) return EFI_OUT_OF_RESOURCES;
    uint64_t bitmap_storage_bytes = bytes_per_bitmap * 2u;
    UINTN bitmap_pages = (UINTN)((bitmap_storage_bytes + ARGUS_PAGE_SIZE - 1u) /
                                 ARGUS_PAGE_SIZE);

    EFI_STATUS status = allocate_pages(bs, 1, &info_range);
    if (status != EFI_SUCCESS) return status;
    status = allocate_pages(bs, KERNEL_STACK_PAGES, &stack_range);
    if (status != EFI_SUCCESS) {
        free_range(bs, info_range);
        return status;
    }
    status = allocate_pages(bs, bitmap_pages, &bitmap_range);
    if (status != EFI_SUCCESS) {
        free_range(bs, stack_range);
        free_range(bs, info_range);
        return status;
    }

    boot_info_t *info = (boot_info_t *)(uintptr_t)info_range.base;
    zero_bytes(info, ARGUS_PAGE_SIZE);
    info->magic = ARGUS_BOOT_INFO_MAGIC;
    info->version = ARGUS_BOOT_INFO_VERSION;
    info->boot_info_storage = info_range;
    info->kernel_stack = stack_range;
    info->pmm_bitmap_storage = bitmap_range;
    info->pmm_page_count = page_count;
    info->pmm_bitmap_bytes = bytes_per_bitmap;
    *info_out = info;
    return EFI_SUCCESS;
}

static void halt_after_handoff_failure(EFI_STATUS status) __attribute__((noreturn));

static void halt_after_handoff_failure(EFI_STATUS status) {
    serial_write("EXIT_BOOT_SERVICES_FAILED: ");
    serial_write_hex64(status);
    serial_write("\n");
    cpu_halt_forever();
}

EFI_STATUS boot_kernel(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table,
    int exception_self_test
) {
    EFI_BOOT_SERVICES *bs = system_table->BootServices;
    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
    VOID *interface = 0;
    EFI_STATUS status = bs->HandleProtocol(image, &loaded_image_guid, &interface);
    if (status != EFI_SUCCESS || !interface) return status != EFI_SUCCESS ? status : EFI_NOT_FOUND;
    loaded_image = (EFI_LOADED_IMAGE_PROTOCOL *)interface;

    memory_map_t preliminary_map;
    status = uefi_memory_map_acquire(bs, &preliminary_map);
    if (status != EFI_SUCCESS) return status;
    uint64_t page_count = highest_tracked_page(&preliminary_map);
    uefi_memory_map_release(bs, &preliminary_map);
    if (!page_count) return EFI_NOT_FOUND;

    boot_info_t *info = 0;
    status = prepare_allocations(bs, page_count, &info);
    if (status != EFI_SUCCESS) return status;

    info->kernel_image.base = (uint64_t)(uintptr_t)loaded_image->ImageBase;
    info->kernel_image.size = loaded_image->ImageSize;
    info->exception_self_test = exception_self_test != 0;
    info->acpi_rsdp = find_acpi_rsdp(system_table);
    capture_framebuffer(info);

    serial_init();
    serial_write("ARGUS_HANDOFF_BEGIN\n");

    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        memory_map_t final_map;
        status = uefi_memory_map_acquire(bs, &final_map);
        if (status != EFI_SUCCESS) {
            if (attempt) halt_after_handoff_failure(status);
            free_range(bs, info->pmm_bitmap_storage);
            free_range(bs, info->kernel_stack);
            free_range(bs, info->boot_info_storage);
            return status;
        }

        info->memory_map = final_map;
        info->memory_map_storage.base = (uint64_t)(uintptr_t)final_map.buffer;
        info->memory_map_storage.size = final_map.capacity;

        /* No firmware or protocol calls may occur between these two operations. */
        status = bs->ExitBootServices(image, final_map.key);
        if (status == EFI_SUCCESS) {
            info->boot_services_exited = 1;
            void *stack_top = (void *)(uintptr_t)(
                info->kernel_stack.base + info->kernel_stack.size);
            cpu_switch_stack_and_call(stack_top, kernel_main, info);
        }

        if (status != EFI_INVALID_PARAMETER) halt_after_handoff_failure(status);
        uefi_memory_map_release(bs, &final_map);
    }

    halt_after_handoff_failure(status);
}
