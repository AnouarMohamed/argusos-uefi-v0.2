#include "ipc.h"

#define IPC_ENDPOINT_TAG 0x4950ULL

typedef struct {
    uint64_t sender_pid;
    uint32_t length;
    uint8_t data[ARGUS_IPC_MESSAGE_MAX];
} ipc_message_t;

typedef struct {
    uint64_t receiver_pid;
    uint32_t generation;
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    int used;
    ipc_message_t messages[ARGUS_IPC_QUEUE_CAPACITY];
} ipc_endpoint_t;

static ipc_endpoint_t endpoints[ARGUS_IPC_ENDPOINT_MAX];

static uint64_t make_endpoint(uint32_t slot, const ipc_endpoint_t *endpoint) {
    return (IPC_ENDPOINT_TAG << 48) |
        ((uint64_t)endpoint->generation << 8) |
        (slot + 1u);
}

static ipc_endpoint_t *resolve_endpoint(uint64_t handle) {
    if (!handle || (handle >> 48) != IPC_ENDPOINT_TAG) return 0;
    uint32_t encoded_slot = (uint32_t)(handle & 0xFFu);
    if (!encoded_slot || encoded_slot > ARGUS_IPC_ENDPOINT_MAX) return 0;
    uint32_t slot = encoded_slot - 1u;
    ipc_endpoint_t *endpoint = &endpoints[slot];
    if (!endpoint->used || make_endpoint(slot, endpoint) != handle) return 0;
    return endpoint;
}

void ipc_init(void) {
    for (uint32_t slot = 0; slot < ARGUS_IPC_ENDPOINT_MAX; ++slot) {
        endpoints[slot] = (ipc_endpoint_t){0};
        endpoints[slot].generation = 0x49500001u ^
            ((slot + 1u) * 0x9E3779B9u);
    }
}

uint64_t ipc_endpoint_create(uint64_t receiver_pid) {
    if (!receiver_pid) return 0;
    for (uint32_t slot = 0; slot < ARGUS_IPC_ENDPOINT_MAX; ++slot) {
        ipc_endpoint_t *endpoint = &endpoints[slot];
        if (endpoint->used) continue;
        uint32_t generation = endpoint->generation;
        *endpoint = (ipc_endpoint_t){0};
        endpoint->generation = generation ? generation : 1u;
        endpoint->receiver_pid = receiver_pid;
        endpoint->used = 1;
        return make_endpoint(slot, endpoint);
    }
    return 0;
}

int ipc_endpoint_destroy(uint64_t handle) {
    ipc_endpoint_t *endpoint = resolve_endpoint(handle);
    if (!endpoint) return 0;
    uint32_t generation = endpoint->generation + 1u;
    if (!generation) generation = 1u;
    *endpoint = (ipc_endpoint_t){0};
    endpoint->generation = generation;
    return 1;
}

int ipc_send(
    uint64_t handle,
    uint64_t sender_pid,
    const uint8_t *data,
    uint32_t length
) {
    ipc_endpoint_t *endpoint = resolve_endpoint(handle);
    if (!endpoint || !sender_pid || !data || !length ||
        length > ARGUS_IPC_MESSAGE_MAX ||
        endpoint->count == ARGUS_IPC_QUEUE_CAPACITY)
        return 0;
    ipc_message_t *message = &endpoint->messages[endpoint->head];
    message->sender_pid = sender_pid;
    message->length = length;
    for (uint32_t index = 0; index < length; ++index)
        message->data[index] = data[index];
    endpoint->head = (uint8_t)(
        (endpoint->head + 1u) % ARGUS_IPC_QUEUE_CAPACITY
    );
    ++endpoint->count;
    return 1;
}

int ipc_receive(
    uint64_t handle,
    uint64_t receiver_pid,
    uint8_t *data,
    uint32_t capacity,
    uint32_t *length,
    uint64_t *sender_pid
) {
    ipc_endpoint_t *endpoint = resolve_endpoint(handle);
    if (!endpoint || endpoint->receiver_pid != receiver_pid || !data ||
        !length || !sender_pid || !endpoint->count)
        return 0;
    const ipc_message_t *message = &endpoint->messages[endpoint->tail];
    if (capacity < message->length) return 0;
    for (uint32_t index = 0; index < message->length; ++index)
        data[index] = message->data[index];
    *length = message->length;
    *sender_pid = message->sender_pid;
    endpoint->tail = (uint8_t)(
        (endpoint->tail + 1u) % ARGUS_IPC_QUEUE_CAPACITY
    );
    --endpoint->count;
    return 1;
}

uint32_t ipc_pending(uint64_t handle, uint64_t receiver_pid) {
    ipc_endpoint_t *endpoint = resolve_endpoint(handle);
    return endpoint && endpoint->receiver_pid == receiver_pid
        ? endpoint->count : 0u;
}

int ipc_self_test(void) {
    ipc_init();
    uint64_t endpoint = ipc_endpoint_create(41u);
    const uint8_t sent[3] = {0x41u, 0x52u, 0x47u};
    uint8_t received[3] = {0};
    uint32_t length = 0;
    uint64_t sender = 0;
    if (!endpoint || !ipc_send(endpoint, 17u, sent, sizeof(sent)) ||
        ipc_pending(endpoint, 41u) != 1u ||
        ipc_receive(endpoint, 42u, received, sizeof(received), &length, &sender) ||
        ipc_receive(endpoint, 41u, received, 2u, &length, &sender) ||
        ipc_pending(endpoint, 41u) != 1u ||
        !ipc_receive(
            endpoint,
            41u,
            received,
            sizeof(received),
            &length,
            &sender) ||
        length != sizeof(sent) || sender != 17u ||
        received[0] != sent[0] || received[1] != sent[1] ||
        received[2] != sent[2] || ipc_pending(endpoint, 41u) ||
        !ipc_endpoint_destroy(endpoint) ||
        ipc_send(endpoint, 17u, sent, sizeof(sent)))
        return 0;
    ipc_init();
    return 1;
}
