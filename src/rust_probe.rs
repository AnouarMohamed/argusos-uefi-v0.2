#![no_std]

const ARGUS_MODULE_ABI_VERSION: u32 = 1;
const ARGUS_MODULE_NAME_CAPACITY: usize = 24;
const FNV_OFFSET_BASIS: u64 = 14_695_981_039_346_656_037;
const FNV_PRIME: u64 = 1_099_511_628_211;

type ChecksumFn = extern "C" fn(*const u8, u64) -> u64;

#[repr(C)]
pub struct ArgusModuleV1 {
    abi_version: u32,
    struct_size: u32,
    name: [u8; ARGUS_MODULE_NAME_CAPACITY],
    checksum: ChecksumFn,
}

const _: [(); 40] = [(); core::mem::size_of::<ArgusModuleV1>()];

extern "C" fn fnv1a(bytes: *const u8, length: u64) -> u64 {
    if bytes.is_null() && length != 0 {
        return 0;
    }

    let mut hash = FNV_OFFSET_BASIS;
    let mut index = 0u64;
    while index < length {
        // SAFETY: ABI v1 requires the caller to provide `length` readable bytes.
        hash ^= unsafe { *bytes.add(index as usize) } as u64;
        hash = hash.wrapping_mul(FNV_PRIME);
        index += 1;
    }
    hash
}

static MODULE: ArgusModuleV1 = ArgusModuleV1 {
    abi_version: ARGUS_MODULE_ABI_VERSION,
    struct_size: core::mem::size_of::<ArgusModuleV1>() as u32,
    name: [
        b'r', b'u', b's', b't', b'.', b'f', b'n', b'v', b'1', b'a', 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    ],
    checksum: fnv1a,
};

#[no_mangle]
pub extern "C" fn argus_rust_module_entry() -> *const ArgusModuleV1 {
    &MODULE
}
