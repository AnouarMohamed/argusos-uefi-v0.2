# ArgusOS network foundation v0.21

ArgusOS v0.21 adds a testable network-device and packet-processing boundary
without enabling network traffic. The release is intentionally non-routable. It
does not provide a socket API, DNS, Internet access, Tor, or anonymity.

## Disconnected test topology

The QEMU harness attaches one modern `virtio-net-pci` function and one unsupported
`e1000e` canary to an internal software hub. The hub has no host network backend,
TAP device, socket, user-mode NAT, or forwarding rule. Requiring both functions
to be quarantined proves discovery does not disable only the selected driver.
The expected QEMU warning that the hub is not connected to a host network is
evidence of this test topology, not an error.

The emulated device has a fixed locally administered MAC address and no option
ROM. It cannot affect boot order or invoke PXE firmware.

## PCI ownership and quarantine

PCI discovery records network-class functions independently from storage. The
network boundary accepts only the modern VirtIO network identity used by the
test harness and validates its PCI capability chain with bounded traversal,
loop detection, BAR checks, region overflow checks, minimum structure lengths,
and required common, notification, and ISR regions.

During discovery, ArgusOS clears and re-reads the I/O-space, memory-space, and
bus-master command bits of every network-class function and sets PCI interrupt
disable. Boot fails if any NIC cannot be quarantined. Only then may the first
supported device be accepted.
Consequently:

- the device cannot perform DMA
- the device cannot signal INTx, MSI, or MSI-X interrupts
- receive and transmit queues are not negotiated with hardware
- MMIO registers are not mapped or activated
- link traffic cannot enter or leave the Rust queues

The kernel holds one generation-checked raw-network reservation for the Tor
transport role. This is not installed in any process capability table. Ordinary
apps, renderers, and the future browser UI still have zero raw-network and zero
anonymous-stream authority.

## Rust network core ABI

`rust.netcore` is a freestanding `no_std` Rust component behind network ABI v1.
It has no allocator and receives a fixed, C-owned, 16-byte-aligned state region.
The state contains separate ingress and egress queues, each limited to eight
frames of at most 1514 bytes. Full queues, empty queues, invalid directions,
undersized output buffers, overlapping ABI regions, bad state, and malformed
frames return explicit errors without changing queue ownership.

Accepted packets are deliberately narrow:

- Ethernet II carrying IPv4
- IPv4 version 4 with a 20-byte header, valid checksum, nonzero TTL, and no fragments
- UDP with exact lengths and a mandatory valid checksum
- TCP with bounded header length, valid flags, nonzero ports, and a valid checksum

IPv4 options, fragmentation, zero UDP checksums, non-IPv4 EtherTypes, and other
transport protocols are rejected. This is stricter than general IPv4
compatibility by design.

The boot self-test constructs valid UDP and TCP fixtures, then mutates EtherType,
IPv4 checksum, fragmentation bits, UDP checksum, lengths, and TCP flag
combinations. It also proves queue-full and undersized-buffer behavior and checks
a small deterministic TCP lifecycle validator.

## TCP scope

The current TCP code validates segment shape/checksum and legal high-level state
transitions. It is not a complete TCP implementation. It has no sequence-window
tracking, reassembly, retransmission, timers, congestion control, path MTU,
SACK, or SYN-cookie policy. `TCP_STATE_CORE_PASS` means only that this bounded
state validator passed its negative tests.

## Release invariants

The `network` shell command and System window report the real state. A v0.21 QEMU
boot must show:

- one validated modern VirtIO function plus one unsupported quarantine canary
- PCI state quarantined
- DMA disabled
- egress disabled
- `rust.netcore` online
- eight-frame bounded queues

The existing `security` command must continue to show clearnet denied, local DNS
denied, anonymous transport offline, and zero process-owned network capabilities.

## Next gate

The NIC must remain quarantined until DMA confinement exists. The next security
milestone is an IOMMU-backed or equivalently isolated DMA domain, fixed receive
and transmit buffers with descriptor validation, and reset/error containment.
Only then should the VirtIO feature negotiation and live queues be enabled.

After contained DMA, the Rust core still needs ARP, ICMP, randomized IPv4 and TCP
identifiers from a kernel CSPRNG, real TCP sequencing/retransmission/timeouts,
and a broker-only socket ABI. DNS, TLS, Arti, and a browser remain later gates.

## Primary references

- [QEMU VirtIO device guidance](https://www.qemu.org/docs/master/system/devices/virtio/)
- [QEMU network emulation and hubs](https://www.qemu.org/docs/master/system/devices/net.html)
- [VirtIO 1.3 initialization and PCI transport](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html)
