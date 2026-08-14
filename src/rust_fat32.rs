#![no_std]

use core::ffi::c_void;
use core::mem::{align_of, size_of};
use core::ptr;

const BLOCK_ABI_VERSION: u32 = 1;
const BLOCK_NAME_CAPACITY: usize = 24;
const FAT32_ABI_VERSION: u32 = 1;
const FAT32_NAME_CAPACITY: usize = 24;
const MAX_PATH: usize = 48;
const MAX_READ: usize = 4096;
const SECTOR_SIZE: usize = 512;
const DIRECTORY_ENTRIES_PER_SECTOR: usize = 16;
const MAX_DIRECTORY_CLUSTERS: u64 = 4096;
const MAX_FILE_CLUSTERS: usize = MAX_READ / SECTOR_SIZE;
const FAT32_MIN_CLUSTERS: u64 = 65525;
const FAT32_MAX_CLUSTERS: u64 = 0x0FFF_FFF5;
const FAT32_EOC: u32 = 0x0FFF_FFF8;
const FAT32_BAD_CLUSTER: u32 = 0x0FFF_FFF7;
const STATE_MAGIC: u64 = 0x4152_4755_5346_4154;

const OK: i32 = 0;
const NOT_FOUND: i32 = -1;
const INVALID: i32 = -2;
const UNSUPPORTED: i32 = -3;
const CORRUPT: i32 = -4;
const IO_ERROR: i32 = -5;
const BUFFER_TOO_SMALL: i32 = -6;

type BlockReadFn = extern "C" fn(*const c_void, u64, u32, *mut u8, u64) -> i32;

#[repr(C)]
struct BlockDeviceV1 {
    abi_version: u32,
    struct_size: u32,
    name: [u8; BLOCK_NAME_CAPACITY],
    sector_size: u32,
    reserved: u32,
    sector_count: u64,
    context: *const c_void,
    read: Option<BlockReadFn>,
}

#[repr(C)]
pub struct Fat32InfoV1 {
    bytes_per_sector: u32,
    sectors_per_cluster: u32,
    fat_count: u32,
    root_cluster: u32,
    total_sectors: u64,
    data_clusters: u64,
    first_data_sector: u64,
}

type MountFn = extern "C" fn(*mut c_void, u64, *const BlockDeviceV1) -> i32;
type InfoFn = extern "C" fn(*const c_void, *mut Fat32InfoV1) -> i32;
type EntryFn = extern "C" fn(
    *const c_void,
    *const BlockDeviceV1,
    u64,
    *mut u8,
    u64,
    *mut u64,
    *mut u64,
    *mut u32,
) -> i32;
type ReadFn = extern "C" fn(
    *const c_void,
    *const BlockDeviceV1,
    *const u8,
    u64,
    *mut u8,
    u64,
    *mut u64,
) -> i32;

#[repr(C)]
pub struct ArgusFat32V1 {
    abi_version: u32,
    struct_size: u32,
    name: [u8; FAT32_NAME_CAPACITY],
    state_size: u32,
    state_alignment: u32,
    max_path: u32,
    max_read: u32,
    reserved: [u32; 2],
    mount: MountFn,
    info: InfoFn,
    entry: EntryFn,
    read: ReadFn,
}

#[repr(C, align(16))]
struct Fat32State {
    magic: u64,
    bytes_per_sector: u32,
    sectors_per_cluster: u32,
    reserved_sectors: u32,
    fat_count: u32,
    fat_size: u64,
    total_sectors: u64,
    root_cluster: u32,
    active_fat: u32,
    first_data_sector: u64,
    data_clusters: u64,
}

#[derive(Clone, Copy)]
struct Input {
    pointer: *const u8,
    length: usize,
}

#[derive(Clone, Copy)]
struct FoundEntry {
    first_cluster: u32,
    file_size: u32,
    attributes: u32,
}

const _: [(); 64] = [(); size_of::<BlockDeviceV1>()];
const _: [(); 40] = [(); size_of::<Fat32InfoV1>()];
const _: [(); 88] = [(); size_of::<ArgusFat32V1>()];

fn checked_add(left: u64, right: u64) -> Option<u64> {
    if left > u64::MAX - right {
        None
    } else {
        Some(left.wrapping_add(right))
    }
}

fn checked_mul(left: u64, right: u64) -> Option<u64> {
    if left != 0 && right > u64::MAX / left {
        None
    } else {
        Some(left.wrapping_mul(right))
    }
}

fn read_u16(bytes: *const u8, offset: usize) -> u16 {
    // SAFETY: callers use constant offsets wholly inside one 512-byte sector.
    unsafe { u16::from(*bytes.add(offset)) | (u16::from(*bytes.add(offset.wrapping_add(1))) << 8) }
}

fn read_u32(bytes: *const u8, offset: usize) -> u32 {
    // SAFETY: callers use constant offsets wholly inside one 512-byte sector.
    unsafe {
        u32::from(*bytes.add(offset))
            | (u32::from(*bytes.add(offset.wrapping_add(1))) << 8)
            | (u32::from(*bytes.add(offset.wrapping_add(2))) << 16)
            | (u32::from(*bytes.add(offset.wrapping_add(3))) << 24)
    }
}

fn input_at(input: Input, index: usize) -> u8 {
    // SAFETY: each caller checks index against input.length.
    unsafe { *input.pointer.add(index) }
}

fn checked_input(pointer: *const u8, length: u64) -> Option<Input> {
    if length == 0 || length > MAX_PATH as u64 || pointer.is_null() {
        return None;
    }
    Some(Input {
        pointer,
        length: length as usize,
    })
}

fn valid_root_path(path: Input) -> bool {
    if path.length < 2 || input_at(path, 0) != b'/' {
        return false;
    }
    let mut index = 1usize;
    while index < path.length {
        let byte = input_at(path, index);
        if byte == b'/' || byte == b'\\' || !(33..=126).contains(&byte) {
            return false;
        }
        index = index.wrapping_add(1);
    }
    true
}

fn valid_device(device: *const BlockDeviceV1) -> bool {
    if device.is_null() || (device as usize) & (align_of::<BlockDeviceV1>() - 1) != 0 {
        return false;
    }
    // SAFETY: non-nullness and alignment were checked; C owns a full descriptor.
    let block = unsafe { &*device };
    block.abi_version == BLOCK_ABI_VERSION
        && block.struct_size == size_of::<BlockDeviceV1>() as u32
        && block.sector_size == SECTOR_SIZE as u32
        && block.reserved == 0
        && block.sector_count != 0
        && block.read.is_some()
}

fn read_sector(device: &BlockDeviceV1, sector: u64, output: *mut u8) -> i32 {
    if output.is_null() || sector >= device.sector_count {
        return IO_ERROR;
    }
    let Some(read) = device.read else {
        return IO_ERROR;
    };
    if read(device.context, sector, 1, output, SECTOR_SIZE as u64) == 0 {
        OK
    } else {
        IO_ERROR
    }
}

fn valid_state(filesystem: &Fat32State) -> bool {
    filesystem.magic == STATE_MAGIC
        && filesystem.bytes_per_sector == SECTOR_SIZE as u32
        && filesystem.sectors_per_cluster != 0
        && filesystem.sectors_per_cluster <= 128
        && filesystem.sectors_per_cluster.is_power_of_two()
        && filesystem.reserved_sectors != 0
        && (filesystem.fat_count == 1 || filesystem.fat_count == 2)
        && filesystem.active_fat < filesystem.fat_count
        && filesystem.fat_size != 0
        && filesystem.root_cluster >= 2
        && filesystem.first_data_sector < filesystem.total_sectors
        && filesystem.data_clusters >= FAT32_MIN_CLUSTERS
        && filesystem.data_clusters < FAT32_MAX_CLUSTERS
}

unsafe fn state_ref<'a>(state: *const c_void) -> Option<&'a Fat32State> {
    if state.is_null() || (state as usize) & (align_of::<Fat32State>() - 1) != 0 {
        return None;
    }
    // SAFETY: C provides an aligned state buffer at least state_size bytes long.
    let filesystem = unsafe { &*state.cast::<Fat32State>() };
    valid_state(filesystem).then_some(filesystem)
}

fn cluster_to_sector(filesystem: &Fat32State, cluster: u32) -> Option<u64> {
    if cluster < 2 || u64::from(cluster) >= filesystem.data_clusters.wrapping_add(2) {
        return None;
    }
    let relative = checked_mul(
        u64::from(cluster - 2),
        u64::from(filesystem.sectors_per_cluster),
    )?;
    let sector = checked_add(filesystem.first_data_sector, relative)?;
    let end = checked_add(sector, u64::from(filesystem.sectors_per_cluster))?;
    (end <= filesystem.total_sectors).then_some(sector)
}

fn read_fat_entry(
    filesystem: &Fat32State,
    device: &BlockDeviceV1,
    cluster: u32,
    value: &mut u32,
) -> i32 {
    let Some(byte_offset) = checked_mul(u64::from(cluster), 4) else {
        return CORRUPT;
    };
    let sector_offset = byte_offset / SECTOR_SIZE as u64;
    if sector_offset >= filesystem.fat_size {
        return CORRUPT;
    }
    let Some(active_offset) = checked_mul(filesystem.fat_size, u64::from(filesystem.active_fat))
    else {
        return CORRUPT;
    };
    let Some(fat_start) = checked_add(u64::from(filesystem.reserved_sectors), active_offset) else {
        return CORRUPT;
    };
    let Some(fat_sector) = checked_add(fat_start, sector_offset) else {
        return CORRUPT;
    };
    let offset = (byte_offset % SECTOR_SIZE as u64) as usize;
    let mut sector = [0u8; SECTOR_SIZE];
    let status = read_sector(device, fat_sector, sector.as_mut_ptr());
    if status != OK {
        return status;
    }
    *value = read_u32(sector.as_ptr(), offset) & 0x0FFF_FFFF;
    OK
}

fn short_name(entry: *const u8, output: *mut u8) -> usize {
    // SAFETY: entry addresses a complete 32-byte directory record and output has 13 bytes.
    unsafe { *output = b'/' };
    let mut used = 1usize;
    let mut base_length = 8usize;
    while base_length != 0 && unsafe { *entry.add(base_length.wrapping_sub(1)) } == b' ' {
        base_length = base_length.wrapping_sub(1);
    }
    let mut index = 0usize;
    while index < base_length {
        // SAFETY: base name occupies bytes 0..8 and output capacity is 13.
        unsafe { *output.add(used) = *entry.add(index) };
        used = used.wrapping_add(1);
        index = index.wrapping_add(1);
    }

    let mut extension_length = 3usize;
    while extension_length != 0
        && unsafe { *entry.add(8usize.wrapping_add(extension_length).wrapping_sub(1)) } == b' '
    {
        extension_length = extension_length.wrapping_sub(1);
    }
    if extension_length != 0 {
        // SAFETY: output capacity is 13 and a short name cannot exceed 12 path bytes.
        unsafe { *output.add(used) = b'.' };
        used = used.wrapping_add(1);
        index = 0;
        while index < extension_length {
            // SAFETY: extension occupies bytes 8..11 and output remains bounded.
            unsafe { *output.add(used) = *entry.add(8usize.wrapping_add(index)) };
            used = used.wrapping_add(1);
            index = index.wrapping_add(1);
        }
    }
    used
}

fn uppercase(byte: u8) -> u8 {
    if byte.is_ascii_lowercase() {
        byte - (b'a' - b'A')
    } else {
        byte
    }
}

fn path_matches(path: Input, name: *const u8, name_length: usize) -> bool {
    if path.length != name_length {
        return false;
    }
    let mut index = 0usize;
    while index < name_length {
        // SAFETY: index is bounded by the generated short-name length.
        if uppercase(input_at(path, index)) != uppercase(unsafe { *name.add(index) }) {
            return false;
        }
        index = index.wrapping_add(1);
    }
    true
}

fn scan_root(
    filesystem: &Fat32State,
    device: &BlockDeviceV1,
    requested_index: u64,
    requested_path: Option<Input>,
    found: &mut FoundEntry,
    path_output: *mut u8,
    path_capacity: u64,
    path_length: *mut u64,
) -> i32 {
    let mut cluster = filesystem.root_cluster;
    let mut visited = 0u64;
    let mut ordinal = 0u64;
    loop {
        if visited >= MAX_DIRECTORY_CLUSTERS {
            return CORRUPT;
        }
        visited = visited.wrapping_add(1);
        let Some(first_sector) = cluster_to_sector(filesystem, cluster) else {
            return CORRUPT;
        };
        let mut sector_offset = 0u32;
        while sector_offset < filesystem.sectors_per_cluster {
            let mut sector = [0u8; SECTOR_SIZE];
            let status = read_sector(
                device,
                first_sector.wrapping_add(u64::from(sector_offset)),
                sector.as_mut_ptr(),
            );
            if status != OK {
                return status;
            }
            let mut slot = 0usize;
            while slot < DIRECTORY_ENTRIES_PER_SECTOR {
                // SAFETY: slot is below 16 and each record is exactly 32 bytes.
                let entry = unsafe { sector.as_ptr().add(slot.wrapping_mul(32)) };
                let first = unsafe { *entry };
                if first == 0 {
                    return NOT_FOUND;
                }
                let attributes = unsafe { *entry.add(11) };
                if first != 0xE5 && first != 0x05 && attributes != 0x0F && attributes & 0x08 == 0 {
                    let mut name = [0u8; 13];
                    let name_length = short_name(entry, name.as_mut_ptr());
                    if name_length <= 1 {
                        return CORRUPT;
                    }
                    let matched = if let Some(path) = requested_path {
                        path_matches(path, name.as_ptr(), name_length)
                    } else {
                        ordinal == requested_index
                    };
                    if matched {
                        let high = u32::from(read_u16(entry, 20));
                        let low = u32::from(read_u16(entry, 26));
                        *found = FoundEntry {
                            first_cluster: (high << 16) | low,
                            file_size: read_u32(entry, 28),
                            attributes: u32::from(attributes),
                        };
                        if !path_length.is_null() {
                            // SAFETY: the ABI requires a writable path-length result.
                            unsafe { *path_length = name_length as u64 };
                        }
                        if !path_output.is_null() {
                            if path_capacity < name_length as u64 {
                                return BUFFER_TOO_SMALL;
                            }
                            // SAFETY: capacity was checked against the generated name.
                            unsafe {
                                ptr::copy_nonoverlapping(name.as_ptr(), path_output, name_length)
                            };
                        }
                        return OK;
                    }
                    ordinal = ordinal.wrapping_add(1);
                }
                slot = slot.wrapping_add(1);
            }
            sector_offset = sector_offset.wrapping_add(1);
        }

        let mut next = 0u32;
        let status = read_fat_entry(filesystem, device, cluster, &mut next);
        if status != OK {
            return status;
        }
        if next >= FAT32_EOC {
            return NOT_FOUND;
        }
        if next < 2 || next == FAT32_BAD_CLUSTER {
            return CORRUPT;
        }
        cluster = next;
    }
}

extern "C" fn mount(
    state: *mut c_void,
    state_size: u64,
    device_pointer: *const BlockDeviceV1,
) -> i32 {
    if state.is_null()
        || state_size < size_of::<Fat32State>() as u64
        || (state as usize) & (align_of::<Fat32State>() - 1) != 0
        || !valid_device(device_pointer)
    {
        return INVALID;
    }
    // SAFETY: the device descriptor was validated above.
    let device = unsafe { &*device_pointer };
    let mut sector = [0u8; SECTOR_SIZE];
    if read_sector(device, 0, sector.as_mut_ptr()) != OK {
        return IO_ERROR;
    }
    if read_u16(sector.as_ptr(), 510) != 0xAA55 {
        return CORRUPT;
    }

    let bytes_per_sector = u32::from(read_u16(sector.as_ptr(), 11));
    let sectors_per_cluster = u32::from(unsafe { *sector.as_ptr().add(13) });
    let reserved_sectors = u32::from(read_u16(sector.as_ptr(), 14));
    let fat_count = u32::from(unsafe { *sector.as_ptr().add(16) });
    let root_entries = read_u16(sector.as_ptr(), 17);
    let total_sectors_16 = read_u16(sector.as_ptr(), 19);
    let fat_size_16 = read_u16(sector.as_ptr(), 22);
    let total_sectors = u64::from(read_u32(sector.as_ptr(), 32));
    let fat_size = u64::from(read_u32(sector.as_ptr(), 36));
    let extended_flags = read_u16(sector.as_ptr(), 40);
    let version = read_u16(sector.as_ptr(), 42);
    let root_cluster = read_u32(sector.as_ptr(), 44);
    let active_fat = if extended_flags & 0x0080 != 0 {
        u32::from(extended_flags & 0x000F)
    } else {
        0
    };

    if bytes_per_sector != SECTOR_SIZE as u32
        || sectors_per_cluster == 0
        || sectors_per_cluster > 128
        || !sectors_per_cluster.is_power_of_two()
        || reserved_sectors == 0
        || (fat_count != 1 && fat_count != 2)
        || root_entries != 0
        || total_sectors_16 != 0
        || fat_size_16 != 0
        || total_sectors == 0
        || fat_size == 0
        || active_fat >= fat_count
        || version != 0
        || root_cluster < 2
    {
        return UNSUPPORTED;
    }
    let Some(fat_area) = checked_mul(u64::from(fat_count), fat_size) else {
        return CORRUPT;
    };
    let Some(first_data_sector) = checked_add(u64::from(reserved_sectors), fat_area) else {
        return CORRUPT;
    };
    if first_data_sector >= total_sectors || total_sectors > device.sector_count {
        return CORRUPT;
    }
    let data_sectors = total_sectors - first_data_sector;
    let data_clusters = data_sectors / u64::from(sectors_per_cluster);
    if !(FAT32_MIN_CLUSTERS..FAT32_MAX_CLUSTERS).contains(&data_clusters)
        || u64::from(root_cluster) >= data_clusters.wrapping_add(2)
    {
        return UNSUPPORTED;
    }
    let Some(fat_bytes) = checked_mul(fat_size, SECTOR_SIZE as u64) else {
        return CORRUPT;
    };
    if fat_bytes / 4 < data_clusters.wrapping_add(2) {
        return CORRUPT;
    }

    // SAFETY: C supplied an aligned state buffer at least Fat32State bytes long.
    unsafe {
        ptr::write_bytes(state.cast::<u8>(), 0, size_of::<Fat32State>());
        let filesystem = &mut *state.cast::<Fat32State>();
        filesystem.magic = STATE_MAGIC;
        filesystem.bytes_per_sector = bytes_per_sector;
        filesystem.sectors_per_cluster = sectors_per_cluster;
        filesystem.reserved_sectors = reserved_sectors;
        filesystem.fat_count = fat_count;
        filesystem.fat_size = fat_size;
        filesystem.total_sectors = total_sectors;
        filesystem.root_cluster = root_cluster;
        filesystem.active_fat = active_fat;
        filesystem.first_data_sector = first_data_sector;
        filesystem.data_clusters = data_clusters;
    }
    OK
}

extern "C" fn info(state: *const c_void, output: *mut Fat32InfoV1) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    // SAFETY: state alignment and invariants are checked before use.
    let Some(filesystem) = (unsafe { state_ref(state) }) else {
        return INVALID;
    };
    // SAFETY: output is non-null and writable by the ABI contract.
    unsafe {
        (*output).bytes_per_sector = filesystem.bytes_per_sector;
        (*output).sectors_per_cluster = filesystem.sectors_per_cluster;
        (*output).fat_count = filesystem.fat_count;
        (*output).root_cluster = filesystem.root_cluster;
        (*output).total_sectors = filesystem.total_sectors;
        (*output).data_clusters = filesystem.data_clusters;
        (*output).first_data_sector = filesystem.first_data_sector;
    }
    OK
}

extern "C" fn entry(
    state: *const c_void,
    device_pointer: *const BlockDeviceV1,
    index: u64,
    path_output: *mut u8,
    path_capacity: u64,
    path_length: *mut u64,
    file_size: *mut u64,
    attributes: *mut u32,
) -> i32 {
    if path_output.is_null()
        || path_length.is_null()
        || file_size.is_null()
        || attributes.is_null()
        || !valid_device(device_pointer)
    {
        return INVALID;
    }
    // SAFETY: state and device validation precede all access.
    let Some(filesystem) = (unsafe { state_ref(state) }) else {
        return INVALID;
    };
    let device = unsafe { &*device_pointer };
    let mut found = FoundEntry {
        first_cluster: 0,
        file_size: 0,
        attributes: 0,
    };
    let status = scan_root(
        filesystem,
        device,
        index,
        None,
        &mut found,
        path_output,
        path_capacity,
        path_length,
    );
    if status == OK {
        // SAFETY: both result pointers are non-null and writable by ABI contract.
        unsafe {
            *file_size = u64::from(found.file_size);
            *attributes = found.attributes;
        }
    }
    status
}

extern "C" fn read_file(
    state: *const c_void,
    device_pointer: *const BlockDeviceV1,
    path_pointer: *const u8,
    path_length: u64,
    output: *mut u8,
    output_capacity: u64,
    output_length: *mut u64,
) -> i32 {
    let Some(path) = checked_input(path_pointer, path_length) else {
        return INVALID;
    };
    if !valid_root_path(path) || output_length.is_null() || !valid_device(device_pointer) {
        return INVALID;
    }
    // SAFETY: state and device validation precede all access.
    let Some(filesystem) = (unsafe { state_ref(state) }) else {
        return INVALID;
    };
    let device = unsafe { &*device_pointer };
    let mut found = FoundEntry {
        first_cluster: 0,
        file_size: 0,
        attributes: 0,
    };
    let status = scan_root(
        filesystem,
        device,
        0,
        Some(path),
        &mut found,
        ptr::null_mut(),
        0,
        ptr::null_mut(),
    );
    if status != OK {
        return status;
    }
    if found.attributes & 0x10 != 0 {
        return UNSUPPORTED;
    }
    let file_size = u64::from(found.file_size);
    // SAFETY: output_length is non-null and writable by the ABI contract.
    unsafe { *output_length = file_size };
    if file_size > MAX_READ as u64 || output_capacity < file_size {
        return BUFFER_TOO_SMALL;
    }
    if file_size == 0 {
        return OK;
    }
    if output.is_null() || found.first_cluster < 2 {
        return INVALID;
    }

    let mut remaining = file_size;
    let mut written = 0u64;
    let mut cluster = found.first_cluster;
    let mut visited = 0usize;
    let mut seen = [0u32; MAX_FILE_CLUSTERS];
    while remaining != 0 {
        if visited >= MAX_FILE_CLUSTERS {
            return CORRUPT;
        }
        let mut previous = 0usize;
        while previous < visited {
            // SAFETY: both indices remain below MAX_FILE_CLUSTERS.
            if unsafe { *seen.as_ptr().add(previous) } == cluster {
                return CORRUPT;
            }
            previous = previous.wrapping_add(1);
        }
        // SAFETY: visited is checked against MAX_FILE_CLUSTERS above.
        unsafe { *seen.as_mut_ptr().add(visited) = cluster };
        visited = visited.wrapping_add(1);

        let Some(first_sector) = cluster_to_sector(filesystem, cluster) else {
            return CORRUPT;
        };
        let mut sector_offset = 0u32;
        while sector_offset < filesystem.sectors_per_cluster && remaining != 0 {
            let mut sector = [0u8; SECTOR_SIZE];
            let status = read_sector(
                device,
                first_sector.wrapping_add(u64::from(sector_offset)),
                sector.as_mut_ptr(),
            );
            if status != OK {
                return status;
            }
            let copied = if remaining < SECTOR_SIZE as u64 {
                remaining as usize
            } else {
                SECTOR_SIZE
            };
            // SAFETY: output capacity was checked against file_size and copied is bounded.
            unsafe {
                ptr::copy_nonoverlapping(sector.as_ptr(), output.add(written as usize), copied)
            };
            written = written.wrapping_add(copied as u64);
            remaining = remaining.wrapping_sub(copied as u64);
            sector_offset = sector_offset.wrapping_add(1);
        }
        if remaining == 0 {
            break;
        }
        let mut next = 0u32;
        let status = read_fat_entry(filesystem, device, cluster, &mut next);
        if status != OK {
            return status;
        }
        if next >= FAT32_EOC || next < 2 || next == FAT32_BAD_CLUSTER {
            return CORRUPT;
        }
        cluster = next;
    }
    OK
}

static MODULE: ArgusFat32V1 = ArgusFat32V1 {
    abi_version: FAT32_ABI_VERSION,
    struct_size: size_of::<ArgusFat32V1>() as u32,
    name: [
        b'r', b'u', b's', b't', b'.', b'f', b'a', b't', b'3', b'2', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0,
    ],
    state_size: size_of::<Fat32State>() as u32,
    state_alignment: align_of::<Fat32State>() as u32,
    max_path: MAX_PATH as u32,
    max_read: MAX_READ as u32,
    reserved: [0, 0],
    mount,
    info,
    entry,
    read: read_file,
};

#[no_mangle]
pub extern "C" fn argus_rust_fat32_entry() -> *const ArgusFat32V1 {
    &MODULE
}
