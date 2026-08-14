# Secure browser requirements

ArgusOS must not ship a decorative browser shell that cannot browse, and it must
not connect a document parser directly to kernel drivers. A credible browser is
a sequence of security gates. Each gate is required before the next one begins.

## Threat model

Assume every network packet, DNS answer, certificate, redirect, compressed body,
HTML byte, image, font, and future script is malicious. Assume a remote origin
will attempt memory corruption, parser confusion, resource exhaustion, origin
confusion, credential theft, and visual spoofing. A browser defect must not grant
ambient access to files, devices, the framebuffer, input queues, or kernel memory.

The initial browser will deliberately support less of the web. Compatibility is
not a reason to weaken isolation or silently bypass a failed security check.

## Required gates

### 1. Process and capability base

Status: process isolation, default-deny capability handles, and bounded IPC are
in place. Delegation, signed packages, and broker processes remain required.

- validated ELF images and non-writable executable mappings
- guarded user stacks, contained user faults, blocking waits, and preemption
- explicit handles with default-deny rights for sockets, storage, clock, and UI
- bounded message IPC with length, ownership, and lifetime validation
- signed application packages and a rollback-safe update path

### 2. Network transport

Status: not implemented.

- a memory-safe network stack where practical, with a narrow C driver boundary
- capability-scoped NIC access owned by the kernel or a dedicated network service
- Ethernet, ARP or neighbor discovery, IPv4 first, ICMP, UDP, and TCP state machines
- randomized source ports and initial sequence values from a kernel CSPRNG
- bounded packet queues, reassembly, retransmission, connection counts, and timeouts
- DNS with strict message parsing, compression-loop detection, response matching,
  cache bounds, and no search-domain leakage by default

### 3. TLS and identity

Status: blocked on transport, time, randomness, and updates.

- an audited TLS library with a maintained security process; no custom cryptography
- TLS 1.3 by default, with older protocol support added only for a justified target
- cryptographically secure randomness and a trustworthy wall clock
- hostname, chain, usage, signature, validity, and revocation policy checks
- a minimal immutable trust store with signed, atomic updates
- strict failure on invalid certificates; the first release has no click-through
- session resumption and ticket storage disabled until origin storage is isolated

### 4. Browser process architecture

Status: blocked on capabilities and IPC.

The browser will use separate roles:

```text
Browser UI process
  | grants narrow requests
  +-> network service
  +-> one renderer process per site boundary
  +-> origin storage service
```

The UI process owns navigation and the trusted origin display. The network
service owns DNS, TCP, and TLS but cannot draw or read user files. Renderers
receive decoded responses through bounded IPC and cannot open sockets, devices,
or arbitrary filesystem paths. Different sites do not share a renderer.

### 5. Document engine

Status: not implemented.

- begin with UTF-8 plain text and a small, non-scripted HTML subset
- use memory-safe parsers where practical and fuzz every parser and state machine
- bound headers, redirects, body bytes, decompression ratio, image dimensions,
  nesting depth, node count, layout work, cache size, and per-origin storage
- block mixed content and cross-origin reads by default
- add URL canonicalization, same-origin checks, HSTS, CSP, cookie rules, and download
  isolation before expanding compatibility
- keep JavaScript disabled until renderer sandboxing, JIT policy, and exploit
  mitigations have their own reviewed design

## Trusted interface

The browser window will use the existing ArgusOS chrome: square one-pixel edges,
warm gray controls, a dark content field, muted slate focus, and the bitmap font.
It will stay compact and keyboard-first. Theme styling never weakens trusted UI.

The browser-owned top strip must always show the canonical host and connection
state in text. Page content cannot draw over, imitate, or reposition it. Security
state must not rely on color alone. TLS errors replace the document view with a
plain explanation and a back action. The first release will not offer an unsafe
proceed button.

## Release gate

Internet browsing remains disabled until transport, audited TLS, certificate and
time validation, capability IPC, renderer isolation, parser limits, fuzzing, and
signed updates all have automated negative tests. Local document viewing may ship
earlier only if it uses the same renderer isolation and cannot obtain a network
or ambient filesystem capability.

ArgusOS v0.20 additionally enforces an offline anonymity policy: ordinary apps,
browser UI, and renderers cannot receive raw-network authority; clearnet and local
DNS are denied; and anonymous connection requests fail unless an isolated Tor
transport and browser network broker are both ready. See
`docs/anonymity-boundary-v0.20.md`.
