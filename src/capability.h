#ifndef ARGUS_CAPABILITY_H
#define ARGUS_CAPABILITY_H

#include "capability_abi.h"
#include <stdint.h>

#define ARGUS_CAPABILITY_MAX 16u

typedef struct {
    uint64_t object;
    uint32_t generation;
    uint16_t type;
    uint16_t rights;
} argus_capability_entry_t;

typedef struct {
    uint64_t principal;
    argus_capability_entry_t entries[ARGUS_CAPABILITY_MAX];
} argus_capability_table_t;

void capability_table_init(
    argus_capability_table_t *table,
    uint64_t principal
);
uint64_t capability_grant(
    argus_capability_table_t *table,
    argus_capability_type_t type,
    uint16_t rights,
    uint64_t object
);
int capability_resolve(
    const argus_capability_table_t *table,
    uint64_t handle,
    argus_capability_type_t type,
    uint16_t required_rights,
    uint64_t *object
);
uint64_t capability_find(
    const argus_capability_table_t *table,
    argus_capability_type_t type,
    uint16_t required_rights
);
int capability_revoke(argus_capability_table_t *table, uint64_t handle);
uint32_t capability_count_type(
    const argus_capability_table_t *table,
    argus_capability_type_t type
);
int capability_self_test(void);

#endif
