#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

use core::ffi::c_void;
use core::mem::{align_of, size_of};
use core::{ptr, slice};

const ABI_VERSION: u32 = 1;
const NAME_CAPACITY: usize = 24;
const MAX_FRAME: usize = 1514;
const QUEUE_CAPACITY: usize = 8;
const STATE_MAGIC: u64 = 0x4152_4755_534E_4554;

const QUEUE_INGRESS: u32 = 0;
const QUEUE_EGRESS: u32 = 1;

const OK: i32 = 0;
const INVALID: i32 = -1;
const UNSUPPORTED: i32 = -2;
const CHECKSUM: i32 = -3;
const FRAGMENT: i32 = -4;
const QUEUE_FULL: i32 = -5;
const BUFFER_TOO_SMALL: i32 = -6;
const QUEUE_EMPTY: i32 = -7;
const STATE_ERROR: i32 = -8;

const TCP_CLOSED: u32 = 0;
const TCP_SYN_SENT: u32 = 1;
const TCP_ESTABLISHED: u32 = 2;
const TCP_FIN_WAIT: u32 = 3;
const TCP_CLOSE_WAIT: u32 = 4;
const TCP_LAST_ACK: u32 = 5;

const TCP_ACTIVE_OPEN: u32 = 1;
const TCP_SYN_ACK: u32 = 2;
const TCP_DATA: u32 = 3;
const TCP_ACTIVE_CLOSE: u32 = 4;
const TCP_PEER_FIN: u32 = 5;
const TCP_ACK: u32 = 6;
const TCP_RESET: u32 = 7;
const TCP_TIMEOUT: u32 = 8;

#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub struct PacketSummaryV1 {
    ether_type: u16,
    ip_total_length: u16,
    protocol: u8,
    tcp_flags: u8,
    transport_header_length: u16,
    source_ipv4: u32,
    destination_ipv4: u32,
    source_port: u16,
    destination_port: u16,
    payload_length: u16,
    reserved: u16,
}

type InitializeFn = extern "C" fn(*mut c_void, u64) -> i32;
type InspectFn = extern "C" fn(*const u8, u64, *mut PacketSummaryV1) -> i32;
type EnqueueFn = extern "C" fn(*mut c_void, u32, *const u8, u64) -> i32;
type DequeueFn = extern "C" fn(*mut c_void, u32, *mut u8, u64, *mut u64) -> i32;
type TcpTransitionFn = extern "C" fn(u32, u32, *mut u32) -> i32;
type SelfTestFn = extern "C" fn() -> i32;

#[repr(C)]
pub struct NetV1 {
    abi_version: u32,
    struct_size: u32,
    name: [u8; NAME_CAPACITY],
    state_size: u32,
    state_alignment: u32,
    max_frame: u32,
    queue_capacity: u32,
    reserved: [u32; 2],
    initialize: InitializeFn,
    inspect: InspectFn,
    enqueue: EnqueueFn,
    dequeue: DequeueFn,
    tcp_transition: TcpTransitionFn,
    self_test: SelfTestFn,
}

struct Packet {
    length: u16,
    bytes: [u8; MAX_FRAME],
}

#[repr(C)]
struct PacketQueue {
    packets: [Packet; QUEUE_CAPACITY],
    head: u8,
    tail: u8,
    count: u8,
    reserved: [u8; 5],
}

#[repr(C, align(16))]
struct NetState {
    magic: u64,
    ingress: PacketQueue,
    egress: PacketQueue,
}

#[repr(C, align(16))]
struct SelfTestStorage([u8; size_of::<NetState>()]);

static mut SELF_TEST_STORAGE: SelfTestStorage = SelfTestStorage([0; size_of::<NetState>()]);

const _: [(); 24] = [(); size_of::<PacketSummaryV1>()];
const _: [(); 104] = [(); size_of::<NetV1>()];

fn read_be16(bytes: &[u8], offset: usize) -> Option<u16> {
    let end = offset.checked_add(2)?;
    let pair = bytes.get(offset..end)?;
    match pair {
        [high, low] => Some(((*high as u16) << 8) | *low as u16),
        _ => None,
    }
}

fn read_be32(bytes: &[u8], offset: usize) -> Option<u32> {
    let end = offset.checked_add(4)?;
    let word = bytes.get(offset..end)?;
    match word {
        [first, second, third, fourth] => Some(
            ((*first as u32) << 24)
                | ((*second as u32) << 16)
                | ((*third as u32) << 8)
                | *fourth as u32,
        ),
        _ => None,
    }
}

fn write_be16(bytes: &mut [u8], offset: usize, value: u16) -> bool {
    let Some(end) = offset.checked_add(2) else {
        return false;
    };
    let Some(pair) = bytes.get_mut(offset..end) else {
        return false;
    };
    match pair {
        [high, low] => {
            *high = (value >> 8) as u8;
            *low = value as u8;
            true
        }
        _ => false,
    }
}

fn checksum_add(mut sum: u32, bytes: &[u8]) -> u32 {
    let mut words = bytes.chunks_exact(2);
    for word in &mut words {
        if let [high, low] = word {
            sum = sum.wrapping_add(((*high as u32) << 8) | *low as u32);
        }
    }
    if let [last] = words.remainder() {
        sum = sum.wrapping_add((*last as u32) << 8);
    }
    sum
}

fn checksum_fold(mut sum: u32) -> u16 {
    while sum >> 16 != 0 {
        sum = (sum & 0xFFFF).wrapping_add(sum >> 16);
    }
    sum as u16
}

fn checksum_valid(bytes: &[u8]) -> bool {
    checksum_fold(checksum_add(0, bytes)) == 0xFFFF
}

fn checksum_value(bytes: &[u8]) -> u16 {
    !checksum_fold(checksum_add(0, bytes))
}

fn transport_checksum_valid(
    source: &[u8],
    destination: &[u8],
    protocol: u8,
    segment: &[u8],
) -> bool {
    if source.len() != 4 || destination.len() != 4 || segment.len() > u16::MAX as usize {
        return false;
    }
    let mut sum = checksum_add(0, source);
    sum = checksum_add(sum, destination);
    sum = sum.wrapping_add(protocol as u32);
    sum = sum.wrapping_add(segment.len() as u32);
    checksum_fold(checksum_add(sum, segment)) == 0xFFFF
}

fn transport_checksum_value(
    source: &[u8],
    destination: &[u8],
    protocol: u8,
    segment: &[u8],
) -> u16 {
    let mut sum = checksum_add(0, source);
    sum = checksum_add(sum, destination);
    sum = sum.wrapping_add(protocol as u32);
    sum = sum.wrapping_add(segment.len() as u32);
    let value = !checksum_fold(checksum_add(sum, segment));
    if value == 0 {
        0xFFFF
    } else {
        value
    }
}

fn inspect_slice(frame: &[u8]) -> Result<PacketSummaryV1, i32> {
    const ETHERNET_HEADER: usize = 14;
    const IPV4_HEADER: usize = 20;
    if frame.len() < ETHERNET_HEADER + IPV4_HEADER || frame.len() > MAX_FRAME {
        return Err(INVALID);
    }
    let ether_type = read_be16(frame, 12).ok_or(INVALID)?;
    if ether_type != 0x0800 {
        return Err(UNSUPPORTED);
    }

    let ip = frame.get(ETHERNET_HEADER..).ok_or(INVALID)?;
    if ip.first().copied() != Some(0x45) {
        return Err(UNSUPPORTED);
    }
    let total_length = read_be16(ip, 2).ok_or(INVALID)? as usize;
    if total_length < IPV4_HEADER || total_length > ip.len() {
        return Err(INVALID);
    }
    let ip_header = ip.get(..IPV4_HEADER).ok_or(INVALID)?;
    if !checksum_valid(ip_header) {
        return Err(CHECKSUM);
    }
    let fragmentation = read_be16(ip, 6).ok_or(INVALID)?;
    if fragmentation & 0xBFFF != 0 {
        return Err(FRAGMENT);
    }
    if ip.get(8).copied().ok_or(INVALID)? == 0 {
        return Err(INVALID);
    }

    let protocol = ip.get(9).copied().ok_or(INVALID)?;
    let transport = ip.get(IPV4_HEADER..total_length).ok_or(INVALID)?;
    let source = ip.get(12..16).ok_or(INVALID)?;
    let destination = ip.get(16..20).ok_or(INVALID)?;
    let mut summary = PacketSummaryV1 {
        ether_type,
        ip_total_length: total_length as u16,
        protocol,
        source_ipv4: read_be32(ip, 12).ok_or(INVALID)?,
        destination_ipv4: read_be32(ip, 16).ok_or(INVALID)?,
        ..PacketSummaryV1::default()
    };

    if protocol == 17 {
        if transport.len() < 8 {
            return Err(INVALID);
        }
        let udp_length = read_be16(transport, 4).ok_or(INVALID)? as usize;
        if udp_length != transport.len() || udp_length < 8 {
            return Err(INVALID);
        }
        if read_be16(transport, 6).ok_or(INVALID)? == 0
            || !transport_checksum_valid(source, destination, protocol, transport)
        {
            return Err(CHECKSUM);
        }
        summary.transport_header_length = 8;
        summary.source_port = read_be16(transport, 0).ok_or(INVALID)?;
        summary.destination_port = read_be16(transport, 2).ok_or(INVALID)?;
        summary.payload_length = udp_length.checked_sub(8).ok_or(INVALID)? as u16;
    } else if protocol == 6 {
        if transport.len() < 20 {
            return Err(INVALID);
        }
        let offset_byte = transport.get(12).copied().ok_or(INVALID)?;
        let header_length = ((offset_byte >> 4) as usize) * 4;
        if header_length < 20 || header_length > 60 || header_length > transport.len() {
            return Err(INVALID);
        }
        if offset_byte & 0x0E != 0 {
            return Err(INVALID);
        }
        let flags = transport.get(13).copied().ok_or(INVALID)?;
        let syn = flags & 0x02 != 0;
        let rst = flags & 0x04 != 0;
        let fin = flags & 0x01 != 0;
        if flags == 0 || (syn && (rst || fin)) || (rst && fin) {
            return Err(INVALID);
        }
        if read_be16(transport, 16).ok_or(INVALID)? == 0
            || !transport_checksum_valid(source, destination, protocol, transport)
        {
            return Err(CHECKSUM);
        }
        summary.tcp_flags = flags;
        summary.transport_header_length = header_length as u16;
        summary.source_port = read_be16(transport, 0).ok_or(INVALID)?;
        summary.destination_port = read_be16(transport, 2).ok_or(INVALID)?;
        summary.payload_length = transport.len().checked_sub(header_length).ok_or(INVALID)? as u16;
    } else {
        return Err(UNSUPPORTED);
    }

    if summary.source_port == 0 || summary.destination_port == 0 {
        return Err(INVALID);
    }
    Ok(summary)
}

fn valid_state_pointer(state: *mut c_void) -> bool {
    !state.is_null()
        && (state as usize) & (align_of::<NetState>() - 1) == 0
        && (state as usize)
            .checked_add(size_of::<NetState>())
            .is_some()
}

fn ranges_overlap(first: usize, first_length: usize, second: usize, second_length: usize) -> bool {
    let Some(first_end) = first.checked_add(first_length) else {
        return true;
    };
    let Some(second_end) = second.checked_add(second_length) else {
        return true;
    };
    first < second_end && second < first_end
}

unsafe fn state_mut<'a>(state: *mut c_void) -> Option<&'a mut NetState> {
    if !valid_state_pointer(state) {
        return None;
    }
    // SAFETY: the caller of this helper supplies exclusive state ownership;
    // pointer shape and initialized storage are checked here before return.
    let network = unsafe { &mut *state.cast::<NetState>() };
    if network.magic != STATE_MAGIC {
        return None;
    }
    Some(network)
}

fn select_queue(state: &mut NetState, queue: u32) -> Option<&mut PacketQueue> {
    if queue == QUEUE_INGRESS {
        Some(&mut state.ingress)
    } else if queue == QUEUE_EGRESS {
        Some(&mut state.egress)
    } else {
        None
    }
}

fn initialize_impl(state: *mut c_void, capacity: u64) -> i32 {
    if !valid_state_pointer(state) || capacity < size_of::<NetState>() as u64 {
        return INVALID;
    }
    // SAFETY: alignment, non-nullness, and capacity were validated above.
    unsafe {
        ptr::write_bytes(state.cast::<u8>(), 0, size_of::<NetState>());
        (*state.cast::<NetState>()).magic = STATE_MAGIC;
    }
    OK
}

extern "C" fn initialize(state: *mut c_void, capacity: u64) -> i32 {
    initialize_impl(state, capacity)
}

extern "C" fn inspect(frame: *const u8, length: u64, summary: *mut PacketSummaryV1) -> i32 {
    if frame.is_null() || summary.is_null() || length == 0 || length > MAX_FRAME as u64 {
        return INVALID;
    }
    if ranges_overlap(
        frame as usize,
        length as usize,
        summary as usize,
        size_of::<PacketSummaryV1>(),
    ) {
        return INVALID;
    }
    // SAFETY: the ABI requires the kernel caller to provide `length` readable bytes.
    let bytes = unsafe { slice::from_raw_parts(frame, length as usize) };
    match inspect_slice(bytes) {
        Ok(parsed) => {
            // SAFETY: non-nullness was checked; unaligned output is supported.
            unsafe { ptr::write_unaligned(summary, parsed) };
            OK
        }
        Err(status) => status,
    }
}

extern "C" fn enqueue(state: *mut c_void, queue: u32, frame: *const u8, length: u64) -> i32 {
    if frame.is_null() || length == 0 || length > MAX_FRAME as u64 {
        return INVALID;
    }
    if !valid_state_pointer(state)
        || ranges_overlap(
            state as usize,
            size_of::<NetState>(),
            frame as usize,
            length as usize,
        )
    {
        return INVALID;
    }
    // SAFETY: the ABI requires the kernel caller to provide `length` readable bytes.
    let bytes = unsafe { slice::from_raw_parts(frame, length as usize) };
    if let Err(status) = inspect_slice(bytes) {
        return status;
    }
    // SAFETY: state_mut validates the pointer, alignment, and initialized magic.
    let Some(network) = (unsafe { state_mut(state) }) else {
        return INVALID;
    };
    let Some(selected) = select_queue(network, queue) else {
        return INVALID;
    };
    if selected.count as usize >= QUEUE_CAPACITY {
        return QUEUE_FULL;
    }
    let slot = selected.head as usize;
    let Some(packet) = selected.packets.get_mut(slot) else {
        return STATE_ERROR;
    };
    let Some(destination) = packet.bytes.get_mut(..bytes.len()) else {
        return STATE_ERROR;
    };
    destination.copy_from_slice(bytes);
    packet.length = bytes.len() as u16;
    selected.head = (slot.wrapping_add(1) % QUEUE_CAPACITY) as u8;
    selected.count = selected.count.wrapping_add(1);
    OK
}

extern "C" fn dequeue(
    state: *mut c_void,
    queue: u32,
    output: *mut u8,
    capacity: u64,
    length: *mut u64,
) -> i32 {
    if output.is_null() || length.is_null() {
        return INVALID;
    }
    if !valid_state_pointer(state)
        || ranges_overlap(
            state as usize,
            size_of::<NetState>(),
            length as usize,
            size_of::<u64>(),
        )
    {
        return INVALID;
    }
    // SAFETY: state_mut validates the pointer, alignment, and initialized magic.
    let Some(network) = (unsafe { state_mut(state) }) else {
        return INVALID;
    };
    let Some(selected) = select_queue(network, queue) else {
        return INVALID;
    };
    if selected.count == 0 {
        return QUEUE_EMPTY;
    }
    let slot = selected.tail as usize;
    let Some(packet) = selected.packets.get_mut(slot) else {
        return STATE_ERROR;
    };
    let packet_length = packet.length as usize;
    if packet_length == 0 || packet_length > MAX_FRAME {
        return STATE_ERROR;
    }
    if capacity < packet_length as u64 {
        return BUFFER_TOO_SMALL;
    }
    if ranges_overlap(
        state as usize,
        size_of::<NetState>(),
        output as usize,
        packet_length,
    ) || ranges_overlap(
        output as usize,
        packet_length,
        length as usize,
        size_of::<u64>(),
    ) {
        return INVALID;
    }
    // SAFETY: the ABI requires `capacity` writable bytes, and the size check above
    // proves that the packet copy stays inside that region.
    unsafe {
        ptr::copy_nonoverlapping(packet.bytes.as_ptr(), output, packet_length);
        ptr::write_unaligned(length, packet_length as u64);
    }
    packet.length = 0;
    selected.tail = (slot.wrapping_add(1) % QUEUE_CAPACITY) as u8;
    selected.count = selected.count.wrapping_sub(1);
    OK
}

fn tcp_transition_impl(state: u32, event: u32) -> Result<u32, i32> {
    if state > TCP_LAST_ACK || event == 0 || event > TCP_TIMEOUT {
        return Err(INVALID);
    }
    if event == TCP_RESET {
        return Ok(TCP_CLOSED);
    }
    match (state, event) {
        (TCP_CLOSED, TCP_ACTIVE_OPEN) => Ok(TCP_SYN_SENT),
        (TCP_SYN_SENT, TCP_SYN_ACK) => Ok(TCP_ESTABLISHED),
        (TCP_SYN_SENT, TCP_TIMEOUT) => Ok(TCP_CLOSED),
        (TCP_ESTABLISHED, TCP_DATA) | (TCP_ESTABLISHED, TCP_ACK) => Ok(TCP_ESTABLISHED),
        (TCP_ESTABLISHED, TCP_ACTIVE_CLOSE) => Ok(TCP_FIN_WAIT),
        (TCP_ESTABLISHED, TCP_PEER_FIN) => Ok(TCP_CLOSE_WAIT),
        (TCP_FIN_WAIT, TCP_ACK) | (TCP_FIN_WAIT, TCP_PEER_FIN) => Ok(TCP_CLOSED),
        (TCP_CLOSE_WAIT, TCP_ACTIVE_CLOSE) => Ok(TCP_LAST_ACK),
        (TCP_LAST_ACK, TCP_ACK) => Ok(TCP_CLOSED),
        _ => Err(STATE_ERROR),
    }
}

extern "C" fn tcp_transition(state: u32, event: u32, next_state: *mut u32) -> i32 {
    if next_state.is_null() {
        return INVALID;
    }
    match tcp_transition_impl(state, event) {
        Ok(next) => {
            // SAFETY: non-nullness was checked; unaligned output is supported.
            unsafe { ptr::write_unaligned(next_state, next) };
            OK
        }
        Err(status) => status,
    }
}

fn build_ipv4_header(frame: &mut [u8], protocol: u8, transport_length: usize) {
    let ip = 14usize;
    frame[ip] = 0x45;
    frame[ip + 1] = 0;
    write_be16(frame, ip + 2, (20 + transport_length) as u16);
    write_be16(frame, ip + 4, 0x1234);
    write_be16(frame, ip + 6, 0x4000);
    frame[ip + 8] = 64;
    frame[ip + 9] = protocol;
    frame[ip + 12..ip + 16].copy_from_slice(&[10, 0, 0, 1]);
    frame[ip + 16..ip + 20].copy_from_slice(&[10, 0, 0, 2]);
    let header_checksum = checksum_value(&frame[ip..ip + 20]);
    write_be16(frame, ip + 10, header_checksum);
}

fn build_udp_fixture(frame: &mut [u8; 46]) {
    frame[..6].copy_from_slice(&[0x52, 0x54, 0, 0x41, 0x52, 0x47]);
    frame[6..12].copy_from_slice(&[0x52, 0x54, 0, 0x53, 0x45, 0x43]);
    write_be16(frame, 12, 0x0800);
    build_ipv4_header(frame, 17, 12);
    let udp = 34usize;
    write_be16(frame, udp, 41000);
    write_be16(frame, udp + 2, 443);
    write_be16(frame, udp + 4, 12);
    frame[udp + 8..udp + 12].copy_from_slice(b"NET1");
    let checksum = transport_checksum_value(&frame[26..30], &frame[30..34], 17, &frame[udp..]);
    write_be16(frame, udp + 6, checksum);
}

fn build_tcp_fixture(frame: &mut [u8; 54]) {
    frame[..6].copy_from_slice(&[0x52, 0x54, 0, 0x41, 0x52, 0x47]);
    frame[6..12].copy_from_slice(&[0x52, 0x54, 0, 0x53, 0x45, 0x43]);
    write_be16(frame, 12, 0x0800);
    build_ipv4_header(frame, 6, 20);
    let tcp = 34usize;
    write_be16(frame, tcp, 42000);
    write_be16(frame, tcp + 2, 443);
    frame[tcp + 4..tcp + 8].copy_from_slice(&[0, 0, 0, 1]);
    frame[tcp + 12] = 0x50;
    frame[tcp + 13] = 0x02;
    write_be16(frame, tcp + 14, 4096);
    let checksum = transport_checksum_value(&frame[26..30], &frame[30..34], 6, &frame[tcp..]);
    write_be16(frame, tcp + 16, checksum);
}

fn parser_self_test() -> bool {
    let mut udp = [0u8; 46];
    build_udp_fixture(&mut udp);
    let Ok(summary) = inspect_slice(&udp) else {
        return false;
    };
    if summary.protocol != 17
        || summary.source_port != 41000
        || summary.destination_port != 443
        || summary.payload_length != 4
    {
        return false;
    }

    let mut corrupt_ip = udp;
    corrupt_ip[24] ^= 1;
    if inspect_slice(&corrupt_ip) != Err(CHECKSUM) {
        return false;
    }
    let mut fragmented = udp;
    fragmented[20] = 0x20;
    fragmented[21] = 0;
    fragmented[24] = 0;
    fragmented[25] = 0;
    let checksum = checksum_value(&fragmented[14..34]);
    write_be16(&mut fragmented, 24, checksum);
    if inspect_slice(&fragmented) != Err(FRAGMENT) {
        return false;
    }
    let mut corrupt_udp = udp;
    corrupt_udp[45] ^= 1;
    if inspect_slice(&corrupt_udp) != Err(CHECKSUM) {
        return false;
    }
    let mut zero_udp_checksum = udp;
    zero_udp_checksum[40] = 0;
    zero_udp_checksum[41] = 0;
    if inspect_slice(&zero_udp_checksum) != Err(CHECKSUM) {
        return false;
    }
    let mut wrong_type = udp;
    wrong_type[12] = 0x08;
    wrong_type[13] = 0x06;
    if inspect_slice(&wrong_type) != Err(UNSUPPORTED) {
        return false;
    }
    if inspect_slice(&udp[..33]) != Err(INVALID) {
        return false;
    }

    let mut tcp = [0u8; 54];
    build_tcp_fixture(&mut tcp);
    let Ok(tcp_summary) = inspect_slice(&tcp) else {
        return false;
    };
    if tcp_summary.protocol != 6
        || tcp_summary.tcp_flags != 0x02
        || tcp_summary.transport_header_length != 20
        || tcp_summary.payload_length != 0
    {
        return false;
    }
    let input_pointer = tcp.as_ptr();
    let overlapping_summary = tcp.as_mut_ptr().cast::<PacketSummaryV1>();
    if inspect(input_pointer, tcp.len() as u64, overlapping_summary) != INVALID {
        return false;
    }
    let mut invalid_flags = tcp;
    invalid_flags[47] = 0x03;
    invalid_flags[50] = 0;
    invalid_flags[51] = 0;
    let tcp_checksum = transport_checksum_value(
        &invalid_flags[26..30],
        &invalid_flags[30..34],
        6,
        &invalid_flags[34..],
    );
    write_be16(&mut invalid_flags, 50, tcp_checksum);
    inspect_slice(&invalid_flags) == Err(INVALID)
}

fn queue_self_test() -> bool {
    let storage = ptr::addr_of_mut!(SELF_TEST_STORAGE).cast::<c_void>();
    if initialize_impl(storage, size_of::<NetState>() as u64) != OK {
        return false;
    }
    let mut frame = [0u8; 46];
    build_udp_fixture(&mut frame);
    let mut index = 0usize;
    while index < QUEUE_CAPACITY {
        if enqueue(storage, QUEUE_INGRESS, frame.as_ptr(), frame.len() as u64) != OK {
            return false;
        }
        index += 1;
    }
    if enqueue(storage, QUEUE_INGRESS, frame.as_ptr(), frame.len() as u64) != QUEUE_FULL {
        return false;
    }
    let mut output = [0u8; 46];
    let mut length = 0u64;
    if dequeue(
        storage,
        QUEUE_INGRESS,
        storage.cast::<u8>(),
        MAX_FRAME as u64,
        &mut length,
    ) != INVALID
    {
        return false;
    }
    if dequeue(storage, QUEUE_INGRESS, output.as_mut_ptr(), 12, &mut length) != BUFFER_TOO_SMALL
        || dequeue(
            storage,
            QUEUE_INGRESS,
            output.as_mut_ptr(),
            output.len() as u64,
            &mut length,
        ) != OK
        || length != frame.len() as u64
        || output != frame
        || enqueue(storage, 7, frame.as_ptr(), frame.len() as u64) != INVALID
    {
        return false;
    }
    initialize_impl(storage, size_of::<NetState>() as u64) == OK
}

fn tcp_self_test() -> bool {
    tcp_transition_impl(TCP_CLOSED, TCP_ACTIVE_OPEN) == Ok(TCP_SYN_SENT)
        && tcp_transition_impl(TCP_SYN_SENT, TCP_SYN_ACK) == Ok(TCP_ESTABLISHED)
        && tcp_transition_impl(TCP_ESTABLISHED, TCP_DATA) == Ok(TCP_ESTABLISHED)
        && tcp_transition_impl(TCP_ESTABLISHED, TCP_ACTIVE_CLOSE) == Ok(TCP_FIN_WAIT)
        && tcp_transition_impl(TCP_FIN_WAIT, TCP_ACK) == Ok(TCP_CLOSED)
        && tcp_transition_impl(TCP_ESTABLISHED, TCP_PEER_FIN) == Ok(TCP_CLOSE_WAIT)
        && tcp_transition_impl(TCP_CLOSE_WAIT, TCP_ACTIVE_CLOSE) == Ok(TCP_LAST_ACK)
        && tcp_transition_impl(TCP_LAST_ACK, TCP_ACK) == Ok(TCP_CLOSED)
        && tcp_transition_impl(TCP_ESTABLISHED, TCP_RESET) == Ok(TCP_CLOSED)
        && tcp_transition_impl(TCP_CLOSED, TCP_DATA) == Err(STATE_ERROR)
        && tcp_transition_impl(99, TCP_DATA) == Err(INVALID)
}

extern "C" fn self_test() -> i32 {
    if parser_self_test() && queue_self_test() && tcp_self_test() {
        1
    } else {
        0
    }
}

static NETWORK: NetV1 = NetV1 {
    abi_version: ABI_VERSION,
    struct_size: size_of::<NetV1>() as u32,
    name: [
        b'r', b'u', b's', b't', b'.', b'n', b'e', b't', b'c', b'o', b'r', b'e', 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0,
    ],
    state_size: size_of::<NetState>() as u32,
    state_alignment: align_of::<NetState>() as u32,
    max_frame: MAX_FRAME as u32,
    queue_capacity: QUEUE_CAPACITY as u32,
    reserved: [0; 2],
    initialize,
    inspect,
    enqueue,
    dequeue,
    tcp_transition,
    self_test,
};

#[no_mangle]
pub extern "C" fn argus_rust_net_entry() -> *const NetV1 {
    &NETWORK
}
