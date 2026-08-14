#ifndef ARGUS_MODULE_H
#define ARGUS_MODULE_H

#include "module_abi.h"

int module_init(void);
int module_self_test(void);
uint64_t module_count(void);
const argus_module_v1_t *module_at(uint64_t index);

#endif
