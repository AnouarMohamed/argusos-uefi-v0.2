#ifndef ARGUS_NET_ABI_H
#define ARGUS_NET_ABI_H

#include <stddef.h>
#include <stdint.h>

#define ARGUS_NET_ABI_VERSION 1u
#define ARGUS_NET_NAME_CAPACITY 24u
#define ARGUS_NET_MAX_FRAME 1514u
#define ARGUS_NET_QUEUE_CAPACITY 8u

#define ARGUS_NET_QUEUE_INGRESS 0u
#define ARGUS_NET_QUEUE_EGRESS 1u

#define ARGUS_NET_OK 0
#define ARGUS_NET_INVALID -1
#define ARGUS_NET_UNSUPPORTED -2
#define ARGUS_NET_CHECKSUM -3
#define ARGUS_NET_FRAGMENT -4
#define ARGUS_NET_QUEUE_FULL -5
#define ARGUS_NET_BUFFER_TOO_SMALL -6
#define ARGUS_NET_QUEUE_EMPTY -7
#define ARGUS_NET_STATE_ERROR -8

#define ARGUS_TCP_STATE_CLOSED 0u
#define ARGUS_TCP_STATE_SYN_SENT 1u
#define ARGUS_TCP_STATE_ESTABLISHED 2u
#define ARGUS_TCP_STATE_FIN_WAIT 3u
#define ARGUS_TCP_STATE_CLOSE_WAIT 4u
#define ARGUS_TCP_STATE_LAST_ACK 5u

#define ARGUS_TCP_EVENT_ACTIVE_OPEN 1u
#define ARGUS_TCP_EVENT_SYN_ACK 2u
#define ARGUS_TCP_EVENT_DATA 3u
#define ARGUS_TCP_EVENT_ACTIVE_CLOSE 4u
#define ARGUS_TCP_EVENT_PEER_FIN 5u
#define ARGUS_TCP_EVENT_ACK 6u
#define ARGUS_TCP_EVENT_RESET 7u
#define ARGUS_TCP_EVENT_TIMEOUT 8u

typedef struct {
    uint16_t ether_type;
    uint16_t ip_total_length;
    uint8_t protocol;
    uint8_t tcp_flags;
    uint16_t transport_header_length;
    uint32_t source_ipv4;
    uint32_t destination_ipv4;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t payload_length;
    uint16_t reserved;
} argus_net_packet_summary_v1_t;

typedef int32_t (*argus_net_initialize_fn)(void *state, uint64_t capacity);
typedef int32_t (*argus_net_inspect_fn)(
    const uint8_t *frame,
    uint64_t length,
    argus_net_packet_summary_v1_t *summary
);
typedef int32_t (*argus_net_enqueue_fn)(
    void *state,
    uint32_t queue,
    const uint8_t *frame,
    uint64_t length
);
typedef int32_t (*argus_net_dequeue_fn)(
    void *state,
    uint32_t queue,
    uint8_t *output,
    uint64_t capacity,
    uint64_t *length
);
typedef int32_t (*argus_net_tcp_transition_fn)(
    uint32_t state,
    uint32_t event,
    uint32_t *next_state
);
typedef int32_t (*argus_net_self_test_fn)(void);

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    char name[ARGUS_NET_NAME_CAPACITY];
    uint32_t state_size;
    uint32_t state_alignment;
    uint32_t max_frame;
    uint32_t queue_capacity;
    uint32_t reserved[2];
    argus_net_initialize_fn initialize;
    argus_net_inspect_fn inspect;
    argus_net_enqueue_fn enqueue;
    argus_net_dequeue_fn dequeue;
    argus_net_tcp_transition_fn tcp_transition;
    argus_net_self_test_fn self_test;
} argus_net_v1_t;

_Static_assert(sizeof(argus_net_packet_summary_v1_t) == 24u,
               "network packet summary ABI v1 layout changed");
_Static_assert(sizeof(argus_net_v1_t) == 104u,
               "network core ABI v1 layout changed");
_Static_assert(offsetof(argus_net_v1_t, initialize) == 56u,
               "network core ABI v1 function layout changed");

const argus_net_v1_t *argus_rust_net_entry(void);

#endif
