#![no_std]

use core::ffi::c_void;
use core::mem::{align_of, size_of};
use core::ptr;

const ABI_VERSION: u32 = 1;
const NAME_CAPACITY: usize = 24;
const FILE_CAPACITY: usize = 16;
const PATH_CAPACITY: usize = 48;
const DATA_CAPACITY: usize = 1024;
const RAMFS_MAGIC: u64 = 0x4152_4755_5352_4653;

const OK: i32 = 0;
const NOT_FOUND: i32 = -1;
const INVALID: i32 = -2;
const NO_SPACE: i32 = -3;
const BUFFER_TOO_SMALL: i32 = -4;

type InitFn = extern "C" fn(*mut c_void, u64) -> i32;
type WriteFn = extern "C" fn(*mut c_void, *const u8, u64, *const u8, u64) -> i32;
type ReadFn = extern "C" fn(*const c_void, *const u8, u64, *mut u8, u64, *mut u64) -> i32;
type EntryFn = extern "C" fn(*const c_void, u64, *mut u8, u64, *mut u64, *mut u64) -> i32;
type RemoveFn = extern "C" fn(*mut c_void, *const u8, u64) -> i32;

#[repr(C)]
pub struct ArgusRamFsV1 {
    abi_version: u32,
    struct_size: u32,
    name: [u8; NAME_CAPACITY],
    state_size: u32,
    state_alignment: u32,
    max_files: u32,
    max_path: u32,
    max_data: u32,
    reserved: u32,
    initialize: InitFn,
    write: WriteFn,
    read: ReadFn,
    entry: EntryFn,
    remove: RemoveFn,
}

#[repr(C)]
struct FileEntry {
    used: u8,
    path_length: u8,
    data_length: u16,
    path: [u8; PATH_CAPACITY],
    data: [u8; DATA_CAPACITY],
}

#[repr(C, align(16))]
struct RamFs {
    magic: u64,
    file_count: u32,
    reserved: u32,
    files: [FileEntry; FILE_CAPACITY],
}

#[derive(Clone, Copy)]
struct Input {
    pointer: *const u8,
    length: usize,
}

const _: [(); 96] = [(); size_of::<ArgusRamFsV1>()];

fn checked_input(pointer: *const u8, length: u64, maximum: usize) -> Option<Input> {
    if length > maximum as u64 || (length != 0 && pointer.is_null()) {
        return None;
    }
    Some(Input {
        pointer,
        length: length as usize,
    })
}

fn input_at(input: Input, index: usize) -> u8 {
    // SAFETY: every caller proves index < input.length before reaching this helper.
    unsafe { *input.pointer.add(index) }
}

fn valid_path(path: Input) -> bool {
    if path.length < 2
        || input_at(path, 0) != b'/'
        || input_at(path, path.length.wrapping_sub(1)) == b'/'
    {
        return false;
    }

    let mut segment_start = 1usize;
    let mut index = 1usize;
    while index <= path.length {
        let boundary = index == path.length || input_at(path, index) == b'/';
        if boundary {
            let segment_length = index.wrapping_sub(segment_start);
            if segment_length == 0
                || (segment_length == 1 && input_at(path, segment_start) == b'.')
                || (segment_length == 2
                    && input_at(path, segment_start) == b'.'
                    && input_at(path, segment_start.wrapping_add(1)) == b'.')
            {
                return false;
            }
            segment_start = index.wrapping_add(1);
        } else {
            let byte = input_at(path, index);
            if !(33..=126).contains(&byte) || byte == b'\\' {
                return false;
            }
        }
        index = index.wrapping_add(1);
    }
    true
}

fn path_matches(entry: &FileEntry, path: Input) -> bool {
    let length = usize::from(entry.path_length);
    if entry.used == 0 || length > PATH_CAPACITY || length != path.length {
        return false;
    }
    let mut index = 0usize;
    while index < length {
        // SAFETY: index is below both the validated path length and entry path length.
        if unsafe { *entry.path.as_ptr().add(index) } != input_at(path, index) {
            return false;
        }
        index = index.wrapping_add(1);
    }
    true
}

fn valid_filesystem(filesystem: &RamFs) -> bool {
    if filesystem.file_count > FILE_CAPACITY as u32 {
        return false;
    }
    let mut live = 0u32;
    let mut index = 0usize;
    while index < FILE_CAPACITY {
        // SAFETY: index is bounded by FILE_CAPACITY.
        let entry = unsafe { &*filesystem.files.as_ptr().add(index) };
        if entry.used != 0 {
            let path_length = usize::from(entry.path_length);
            if path_length > PATH_CAPACITY
                || usize::from(entry.data_length) > DATA_CAPACITY
                || !valid_path(Input {
                    pointer: entry.path.as_ptr(),
                    length: path_length,
                })
            {
                return false;
            }
            live = live.wrapping_add(1);
        } else if entry.path_length != 0 || entry.data_length != 0 {
            return false;
        }
        index = index.wrapping_add(1);
    }
    live == filesystem.file_count
}

unsafe fn state_mut<'a>(state: *mut c_void) -> Option<&'a mut RamFs> {
    if state.is_null() || (state as usize) & (align_of::<RamFs>() - 1) != 0 {
        return None;
    }
    // SAFETY: alignment and non-nullness were checked; C provides state_size bytes.
    let filesystem = unsafe { &mut *state.cast::<RamFs>() };
    if filesystem.magic != RAMFS_MAGIC || !valid_filesystem(filesystem) {
        return None;
    }
    Some(filesystem)
}

unsafe fn state_ref<'a>(state: *const c_void) -> Option<&'a RamFs> {
    if state.is_null() || (state as usize) & (align_of::<RamFs>() - 1) != 0 {
        return None;
    }
    // SAFETY: alignment and non-nullness were checked; C provides state_size bytes.
    let filesystem = unsafe { &*state.cast::<RamFs>() };
    if filesystem.magic != RAMFS_MAGIC || !valid_filesystem(filesystem) {
        return None;
    }
    Some(filesystem)
}

extern "C" fn initialize(state: *mut c_void, state_size: u64) -> i32 {
    if state.is_null()
        || state_size < size_of::<RamFs>() as u64
        || (state as usize) & (align_of::<RamFs>() - 1) != 0
    {
        return INVALID;
    }
    // SAFETY: the caller supplied a checked, aligned buffer large enough for RamFs.
    unsafe {
        ptr::write_bytes(state.cast::<u8>(), 0, size_of::<RamFs>());
        (*state.cast::<RamFs>()).magic = RAMFS_MAGIC;
    }
    OK
}

extern "C" fn write_file(
    state: *mut c_void,
    path_pointer: *const u8,
    path_length: u64,
    data_pointer: *const u8,
    data_length: u64,
) -> i32 {
    let Some(path) = checked_input(path_pointer, path_length, PATH_CAPACITY) else {
        return INVALID;
    };
    let Some(data) = checked_input(data_pointer, data_length, DATA_CAPACITY) else {
        return NO_SPACE;
    };
    if !valid_path(path) {
        return INVALID;
    }
    // SAFETY: the state pointer and magic are checked before it is exposed as RamFs.
    let Some(filesystem) = (unsafe { state_mut(state) }) else {
        return INVALID;
    };

    let mut existing = FILE_CAPACITY;
    let mut free = FILE_CAPACITY;
    let mut index = 0usize;
    while index < FILE_CAPACITY {
        // SAFETY: index is bounded by FILE_CAPACITY.
        let entry = unsafe { &*filesystem.files.as_ptr().add(index) };
        if path_matches(entry, path) {
            existing = index;
            break;
        }
        if free == FILE_CAPACITY && entry.used == 0 {
            free = index;
        }
        index = index.wrapping_add(1);
    }
    let slot = if existing < FILE_CAPACITY {
        existing
    } else {
        free
    };
    if slot == FILE_CAPACITY {
        return NO_SPACE;
    }

    // SAFETY: slot was selected from the bounded file array.
    let entry = unsafe { &mut *filesystem.files.as_mut_ptr().add(slot) };
    if entry.used == 0 {
        filesystem.file_count = filesystem.file_count.wrapping_add(1);
    }
    entry.used = 1;
    entry.path_length = path.length as u8;
    entry.data_length = data.length as u16;
    // SAFETY: both destinations are fixed arrays and lengths were capped above.
    unsafe {
        ptr::write_bytes(entry.path.as_mut_ptr(), 0, PATH_CAPACITY);
        ptr::write_bytes(entry.data.as_mut_ptr(), 0, DATA_CAPACITY);
        ptr::copy_nonoverlapping(path.pointer, entry.path.as_mut_ptr(), path.length);
        if data.length != 0 {
            ptr::copy_nonoverlapping(data.pointer, entry.data.as_mut_ptr(), data.length);
        }
    }
    OK
}

extern "C" fn read_file(
    state: *const c_void,
    path_pointer: *const u8,
    path_length: u64,
    output: *mut u8,
    output_capacity: u64,
    output_length: *mut u64,
) -> i32 {
    let Some(path) = checked_input(path_pointer, path_length, PATH_CAPACITY) else {
        return INVALID;
    };
    if !valid_path(path) || output_length.is_null() {
        return INVALID;
    }
    // SAFETY: the state pointer and magic are checked before it is exposed as RamFs.
    let Some(filesystem) = (unsafe { state_ref(state) }) else {
        return INVALID;
    };

    let mut index = 0usize;
    while index < FILE_CAPACITY {
        // SAFETY: index is bounded by FILE_CAPACITY.
        let entry = unsafe { &*filesystem.files.as_ptr().add(index) };
        if path_matches(entry, path) {
            let length = usize::from(entry.data_length);
            // SAFETY: output_length is non-null and writable by ABI contract.
            unsafe { *output_length = length as u64 };
            if output_capacity < length as u64 {
                return BUFFER_TOO_SMALL;
            }
            if length != 0 {
                if output.is_null() {
                    return INVALID;
                }
                // SAFETY: output capacity was checked and entry length is bounded.
                unsafe { ptr::copy_nonoverlapping(entry.data.as_ptr(), output, length) };
            }
            return OK;
        }
        index = index.wrapping_add(1);
    }
    NOT_FOUND
}

extern "C" fn entry_at(
    state: *const c_void,
    requested: u64,
    path_output: *mut u8,
    path_capacity: u64,
    path_length: *mut u64,
    data_length: *mut u64,
) -> i32 {
    if path_length.is_null() || data_length.is_null() {
        return INVALID;
    }
    // SAFETY: the state pointer and magic are checked before it is exposed as RamFs.
    let Some(filesystem) = (unsafe { state_ref(state) }) else {
        return INVALID;
    };
    let mut ordinal = 0u64;
    let mut index = 0usize;
    while index < FILE_CAPACITY {
        // SAFETY: index is bounded by FILE_CAPACITY.
        let entry = unsafe { &*filesystem.files.as_ptr().add(index) };
        if entry.used != 0 {
            if ordinal == requested {
                let length = usize::from(entry.path_length);
                // SAFETY: result pointers are non-null and writable by ABI contract.
                unsafe {
                    *path_length = length as u64;
                    *data_length = u64::from(entry.data_length);
                }
                if path_capacity < length as u64 {
                    return BUFFER_TOO_SMALL;
                }
                if length != 0 {
                    if path_output.is_null() {
                        return INVALID;
                    }
                    // SAFETY: output capacity and stored path length were checked.
                    unsafe { ptr::copy_nonoverlapping(entry.path.as_ptr(), path_output, length) };
                }
                return OK;
            }
            ordinal = ordinal.wrapping_add(1);
        }
        index = index.wrapping_add(1);
    }
    NOT_FOUND
}

extern "C" fn remove_file(state: *mut c_void, path_pointer: *const u8, path_length: u64) -> i32 {
    let Some(path) = checked_input(path_pointer, path_length, PATH_CAPACITY) else {
        return INVALID;
    };
    if !valid_path(path) {
        return INVALID;
    }
    // SAFETY: the state pointer and magic are checked before it is exposed as RamFs.
    let Some(filesystem) = (unsafe { state_mut(state) }) else {
        return INVALID;
    };

    let mut index = 0usize;
    while index < FILE_CAPACITY {
        // SAFETY: index is bounded by FILE_CAPACITY.
        let entry = unsafe { &mut *filesystem.files.as_mut_ptr().add(index) };
        if path_matches(entry, path) {
            entry.used = 0;
            entry.path_length = 0;
            entry.data_length = 0;
            // SAFETY: both destinations are their complete fixed arrays.
            unsafe {
                ptr::write_bytes(entry.path.as_mut_ptr(), 0, PATH_CAPACITY);
                ptr::write_bytes(entry.data.as_mut_ptr(), 0, DATA_CAPACITY);
            }
            filesystem.file_count = filesystem.file_count.wrapping_sub(1);
            return OK;
        }
        index = index.wrapping_add(1);
    }
    NOT_FOUND
}

static MODULE: ArgusRamFsV1 = ArgusRamFsV1 {
    abi_version: ABI_VERSION,
    struct_size: size_of::<ArgusRamFsV1>() as u32,
    name: [
        b'r', b'u', b's', b't', b'.', b'r', b'a', b'm', b'f', b's', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0,
    ],
    state_size: size_of::<RamFs>() as u32,
    state_alignment: align_of::<RamFs>() as u32,
    max_files: FILE_CAPACITY as u32,
    max_path: PATH_CAPACITY as u32,
    max_data: DATA_CAPACITY as u32,
    reserved: 0,
    initialize,
    write: write_file,
    read: read_file,
    entry: entry_at,
    remove: remove_file,
};

#[no_mangle]
pub extern "C" fn argus_rust_ramfs_entry() -> *const ArgusRamFsV1 {
    &MODULE
}
