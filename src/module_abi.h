#ifndef ARGUS_MODULE_ABI_H
#define ARGUS_MODULE_ABI_H

#include <stddef.h>
#include <stdint.h>

#define ARGUS_MODULE_ABI_VERSION 1u
#define ARGUS_MODULE_NAME_CAPACITY 24u

typedef uint64_t (*argus_checksum_fn_t)(const uint8_t *bytes, uint64_t length);

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    char name[ARGUS_MODULE_NAME_CAPACITY];
    argus_checksum_fn_t checksum;
} argus_module_v1_t;

_Static_assert(sizeof(argus_module_v1_t) == 40u,
               "module ABI v1 layout must remain stable");
_Static_assert(offsetof(argus_module_v1_t, abi_version) == 0u,
               "module ABI version offset changed");
_Static_assert(offsetof(argus_module_v1_t, struct_size) == 4u,
               "module ABI size offset changed");
_Static_assert(offsetof(argus_module_v1_t, name) == 8u,
               "module ABI name offset changed");
_Static_assert(offsetof(argus_module_v1_t, checksum) == 32u,
               "module ABI function offset changed");

/* Implemented by the bounded no_std Rust object. */
const argus_module_v1_t *argus_rust_module_entry(void);

#endif
