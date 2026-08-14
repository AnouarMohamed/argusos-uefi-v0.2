#include "capability.h"

#define CAPABILITY_HANDLE_TAG 0xA6C5ULL

static uint16_t allowed_rights(argus_capability_type_t type) {
    if (type == ARGUS_CAPABILITY_CLOCK) return ARGUS_CAP_RIGHT_READ;
    if (type == ARGUS_CAPABILITY_INPUT)
        return ARGUS_CAP_RIGHT_READ | ARGUS_CAP_RIGHT_WAIT;
    if (type == ARGUS_CAPABILITY_SURFACE) return ARGUS_CAP_RIGHT_PRESENT;
    if (type == ARGUS_CAPABILITY_IPC)
        return ARGUS_CAP_RIGHT_SEND | ARGUS_CAP_RIGHT_RECEIVE;
    if (type == ARGUS_CAPABILITY_ANONYMOUS_STREAM ||
        type == ARGUS_CAPABILITY_RAW_NETWORK)
        return ARGUS_CAP_RIGHT_CONNECT;
    return 0;
}

static uint32_t initial_generation(uint64_t principal, uint32_t slot) {
    uint32_t mixed = (uint32_t)principal ^ (uint32_t)(principal >> 32) ^
        0xA6C50001u ^ ((slot + 1u) * 0x9E3779B9u);
    return mixed ? mixed : slot + 1u;
}

static uint64_t make_handle(
    uint32_t slot,
    const argus_capability_entry_t *entry
) {
    return (CAPABILITY_HANDLE_TAG << 48) |
        ((uint64_t)entry->generation << 16) |
        ((uint64_t)entry->type << 8) |
        (slot + 1u);
}

void capability_table_init(
    argus_capability_table_t *table,
    uint64_t principal
) {
    if (!table) return;
    *table = (argus_capability_table_t){0};
    table->principal = principal;
    for (uint32_t slot = 0; slot < ARGUS_CAPABILITY_MAX; ++slot)
        table->entries[slot].generation = initial_generation(principal, slot);
}

uint64_t capability_grant(
    argus_capability_table_t *table,
    argus_capability_type_t type,
    uint16_t rights,
    uint64_t object
) {
    uint16_t valid = allowed_rights(type);
    if (!table || !table->principal || !valid || !rights ||
        (rights & ~valid))
        return 0;
    for (uint32_t slot = 0; slot < ARGUS_CAPABILITY_MAX; ++slot) {
        argus_capability_entry_t *entry = &table->entries[slot];
        if (entry->type != ARGUS_CAPABILITY_NONE) continue;
        entry->object = object;
        entry->type = (uint16_t)type;
        entry->rights = rights;
        return make_handle(slot, entry);
    }
    return 0;
}

int capability_resolve(
    const argus_capability_table_t *table,
    uint64_t handle,
    argus_capability_type_t type,
    uint16_t required_rights,
    uint64_t *object
) {
    if (!table || !handle || !table->principal ||
        (handle >> 48) != CAPABILITY_HANDLE_TAG)
        return 0;
    uint32_t encoded_slot = (uint32_t)(handle & 0xFFu);
    if (!encoded_slot || encoded_slot > ARGUS_CAPABILITY_MAX) return 0;
    uint32_t slot = encoded_slot - 1u;
    const argus_capability_entry_t *entry = &table->entries[slot];
    uint32_t generation = (uint32_t)((handle >> 16) & 0xFFFFFFFFu);
    uint16_t encoded_type = (uint16_t)((handle >> 8) & 0xFFu);
    if (entry->type != type || encoded_type != type ||
        entry->generation != generation ||
        (entry->rights & required_rights) != required_rights)
        return 0;
    if (object) *object = entry->object;
    return 1;
}

uint64_t capability_find(
    const argus_capability_table_t *table,
    argus_capability_type_t type,
    uint16_t required_rights
) {
    if (!table || !table->principal) return 0;
    for (uint32_t slot = 0; slot < ARGUS_CAPABILITY_MAX; ++slot) {
        const argus_capability_entry_t *entry = &table->entries[slot];
        if (entry->type == type &&
            (entry->rights & required_rights) == required_rights)
            return make_handle(slot, entry);
    }
    return 0;
}

int capability_revoke(argus_capability_table_t *table, uint64_t handle) {
    if (!table || !handle || (handle >> 48) != CAPABILITY_HANDLE_TAG)
        return 0;
    uint32_t encoded_slot = (uint32_t)(handle & 0xFFu);
    if (!encoded_slot || encoded_slot > ARGUS_CAPABILITY_MAX) return 0;
    uint32_t slot = encoded_slot - 1u;
    argus_capability_entry_t *entry = &table->entries[slot];
    if (make_handle(slot, entry) != handle ||
        entry->type == ARGUS_CAPABILITY_NONE)
        return 0;
    uint32_t next_generation = entry->generation + 1u;
    if (!next_generation) next_generation = 1u;
    *entry = (argus_capability_entry_t){0};
    entry->generation = next_generation;
    return 1;
}

uint32_t capability_count_type(
    const argus_capability_table_t *table,
    argus_capability_type_t type
) {
    if (!table) return 0;
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < ARGUS_CAPABILITY_MAX; ++slot)
        if (table->entries[slot].type == type) ++count;
    return count;
}

int capability_self_test(void) {
    argus_capability_table_t first;
    argus_capability_table_t second;
    capability_table_init(&first, 11u);
    capability_table_init(&second, 12u);
    uint64_t clock = capability_grant(
        &first,
        ARGUS_CAPABILITY_CLOCK,
        ARGUS_CAP_RIGHT_READ,
        0x11223344u
    );
    uint64_t object = 0;
    if (!clock || !capability_resolve(
            &first,
            clock,
            ARGUS_CAPABILITY_CLOCK,
            ARGUS_CAP_RIGHT_READ,
            &object) ||
        object != 0x11223344u ||
        capability_resolve(
            &first,
            clock,
            ARGUS_CAPABILITY_CLOCK,
            ARGUS_CAP_RIGHT_WRITE,
            0) ||
        capability_resolve(
            &first,
            clock ^ (1ULL << 20),
            ARGUS_CAPABILITY_CLOCK,
            ARGUS_CAP_RIGHT_READ,
            0) ||
        capability_resolve(
            &second,
            clock,
            ARGUS_CAPABILITY_CLOCK,
            ARGUS_CAP_RIGHT_READ,
            0) ||
        capability_count_type(&first, ARGUS_CAPABILITY_CLOCK) != 1u ||
        !capability_revoke(&first, clock) ||
        capability_resolve(
            &first,
            clock,
            ARGUS_CAPABILITY_CLOCK,
            ARGUS_CAP_RIGHT_READ,
            0))
        return 0;
    uint64_t replacement = capability_grant(
        &first,
        ARGUS_CAPABILITY_CLOCK,
        ARGUS_CAP_RIGHT_READ,
        7u
    );
    return replacement && replacement != clock &&
           !capability_grant(
               &first,
               ARGUS_CAPABILITY_RAW_NETWORK,
               ARGUS_CAP_RIGHT_READ,
               0u);
}
