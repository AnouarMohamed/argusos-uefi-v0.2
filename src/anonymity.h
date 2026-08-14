#ifndef ARGUS_ANONYMITY_H
#define ARGUS_ANONYMITY_H

#include "capability.h"
#include <stdint.h>

typedef enum {
    ARGUS_SECURITY_ROLE_UTILITY = 1,
    ARGUS_SECURITY_ROLE_BROWSER_UI = 2,
    ARGUS_SECURITY_ROLE_RENDERER = 3,
    ARGUS_SECURITY_ROLE_BROWSER_NETWORK = 4,
    ARGUS_SECURITY_ROLE_TOR_TRANSPORT = 5,
} argus_security_role_t;

typedef enum {
    ARGUS_ANON_TRANSPORT_OFFLINE = 0,
    ARGUS_ANON_TRANSPORT_BOOTSTRAPPING = 1,
    ARGUS_ANON_TRANSPORT_READY = 2,
    ARGUS_ANON_TRANSPORT_FAILED = 3,
} argus_anonymity_transport_state_t;

void anonymity_policy_init(void);
int anonymity_capability_allowed(
    argus_security_role_t role,
    argus_capability_type_t type,
    uint16_t rights
);
int anonymity_transport_update(
    argus_security_role_t caller,
    argus_anonymity_transport_state_t next_state,
    int authenticated
);
int anonymity_connection_allowed(
    argus_security_role_t role,
    argus_capability_type_t type,
    uint16_t rights
);
argus_anonymity_transport_state_t anonymity_transport_state(void);
const char *anonymity_transport_state_name(void);
int anonymity_clearnet_allowed(void);
int anonymity_local_dns_allowed(void);
int anonymity_policy_self_test(void);

#endif
