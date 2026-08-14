# ArgusOS anonymity boundary v0.20

ArgusOS v0.20 establishes a default-deny authority boundary for future network
services. It does not provide Internet access or claim anonymity. Its purpose is
to ensure that adding a network driver later cannot silently give every process
clearnet, DNS, or raw-device access.

## Capability handles

Each process owns a private sixteen-entry capability table. A handle identifies
a table slot, capability type, and generation. Resolution always occurs against
the current process table, so a handle copied or guessed from another process
does not name authority in the caller.

Capabilities have a fixed type-specific rights mask:

- clock: read
- focused input: read and wait
- indexed surface: present
- IPC endpoint: send or receive
- anonymous stream: connect
- raw network: connect

The kernel rejects stale generations, altered handles, wrong types, missing
rights, invalid rights for a type, and cross-process handles. Revocation advances
the slot generation before reuse.

Each process also carries an explicit security role. Network-capability grants
go through a role-checking wrapper before reaching its private table, and
capability lookup requests a specific type and rights set.

Graphical applications now present explicit handles for every clock, input,
wait, and surface operation. App identity alone no longer grants those syscalls.

## Bounded IPC

The initial IPC service provides eight fixed endpoints. Each endpoint has one
receiving PID, an eight-message queue, and a maximum payload of 64 bytes. Send
and receive syscalls require matching per-process capability rights. Queue-full,
empty, stale-endpoint, wrong-receiver, oversized-message, undersized-buffer, and
unmapped or read-only user-buffer cases fail without changing queue ownership.

Every app receives only a receive handle for its private endpoint. No process
currently receives a send handle, network handle, or endpoint-transfer syscall.
The next broker milestone will add narrowly reviewed kernel-side delegation.

## Fail-closed anonymity policy

The policy separates five roles: utility app, browser UI, renderer, browser
network broker, and Tor transport.

- only the Tor transport role may receive raw-network authority
- only the browser network broker may receive anonymous-stream authority
- browser UI, renderers, and ordinary apps receive neither
- clearnet is always denied
- local DNS is always denied
- anonymous connections require both an explicit handle and a ready Tor transport
- transport state cannot move directly from offline to ready
- only an authenticated Tor transport transition can change transport state

The current transport state is offline. Consequently every connection attempt
fails closed. Two ring-3 boot probes attempt an anonymous connection without a
capability and must receive a denial before kernel startup continues.

The `security` shell command reports the real policy state, capability counts,
and denied request count. It does not display a synthetic connected state.

## Arti integration gate

Arti is the preferred Tor implementation because it is maintained by the Tor
Project and written in Rust. It is not vendored yet. Current Arti expects an
asynchronous runtime with TCP, TLS, timers, persistent state, secure randomness,
and an update path. ArgusOS does not yet provide those services, and a partial
port would create false confidence around cryptographic and anonymity behavior.

The safe integration sequence is:

1. capability-owned NIC driver and bounded packet queues
2. memory-safe Ethernet, IPv4, UDP, and TCP service
3. kernel CSPRNG, secure wall clock, and signed atomic updates
4. Rust userspace allocator, async executor, filesystem state, and IPC adapters
5. an Arti `tor_rtcompat` backend or isolated Arti service
6. authenticated bootstrap, directory validation, stream isolation, bridges,
   and forced DNS through Tor
7. browser network broker and isolated renderer processes

ArgusOS v0.21 completes only the disconnected portion of steps 1 and 2: the NIC
is role-reserved but quarantined with DMA disabled, and the Rust component
provides strict packet validation, bounded queues, and a small TCP state core.
It is not a live NIC or complete TCP service. See
`docs/network-foundation-v0.21.md`.

Arti must remain replaceable and regularly updatable. The project will not fork
Tor cryptography or freeze an obsolete protocol implementation.

## Limits

No design can promise that a user is untraceable. Logging into an identifying
account, reusing behavior or content, endpoint compromise, browser fingerprint
differences, malicious peripherals, and large-scale traffic correlation can
defeat anonymity. ArgusOS must state these limits plainly and use maintained Tor
Browser or Tails for sensitive activity until its own implementation reaches a
comparable review and update standard.

## Upstream references

- [Arti client status and embedding guidance](https://arti.torproject.org/FAQs/)
- [Arti client runtime requirements](https://docs.rs/arti-client/latest/arti_client/)
- [Tor stream isolation](https://spec.torproject.org/path-spec/stream-isolation.html)
- [Tor Browser fingerprinting protections](https://support.torproject.org/tor-browser/features/fingerprinting-protections/)
- [Tor Project VPN guidance](https://support.torproject.org/tor-browser/general/vpn-with-tor/)
