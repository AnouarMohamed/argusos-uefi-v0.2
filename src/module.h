#ifndef ARGUS_MODULE_H
#define ARGUS_MODULE_H

#include "module_abi.h"
#include "net_abi.h"

int module_init(void);
int module_self_test(void);
uint64_t module_count(void);
const char *module_name_at(uint64_t index);
uint32_t module_abi_version_at(uint64_t index);
const argus_net_v1_t *module_net_descriptor(void);

#endif
