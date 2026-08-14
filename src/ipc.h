#ifndef ARGUS_IPC_H
#define ARGUS_IPC_H

#include <stdint.h>

#define ARGUS_IPC_ENDPOINT_MAX 8u
#define ARGUS_IPC_QUEUE_CAPACITY 8u
#define ARGUS_IPC_MESSAGE_MAX 64u

void ipc_init(void);
uint64_t ipc_endpoint_create(uint64_t receiver_pid);
int ipc_endpoint_destroy(uint64_t endpoint);
int ipc_send(
    uint64_t endpoint,
    uint64_t sender_pid,
    const uint8_t *data,
    uint32_t length
);
int ipc_receive(
    uint64_t endpoint,
    uint64_t receiver_pid,
    uint8_t *data,
    uint32_t capacity,
    uint32_t *length,
    uint64_t *sender_pid
);
uint32_t ipc_pending(uint64_t endpoint, uint64_t receiver_pid);
int ipc_self_test(void);

#endif
