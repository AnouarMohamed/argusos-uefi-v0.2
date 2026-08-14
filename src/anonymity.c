#include "anonymity.h"

static argus_anonymity_transport_state_t transport_state;

void anonymity_policy_init(void) {
    transport_state = ARGUS_ANON_TRANSPORT_OFFLINE;
}

int anonymity_capability_allowed(
    argus_security_role_t role,
    argus_capability_type_t type,
    uint16_t rights
) {
    if (rights != ARGUS_CAP_RIGHT_CONNECT) return 0;
    if (type == ARGUS_CAPABILITY_RAW_NETWORK)
        return role == ARGUS_SECURITY_ROLE_TOR_TRANSPORT;
    if (type == ARGUS_CAPABILITY_ANONYMOUS_STREAM)
        return role == ARGUS_SECURITY_ROLE_BROWSER_NETWORK;
    return 0;
}

int anonymity_transport_update(
    argus_security_role_t caller,
    argus_anonymity_transport_state_t next_state,
    int authenticated
) {
    if (caller != ARGUS_SECURITY_ROLE_TOR_TRANSPORT || !authenticated ||
        next_state > ARGUS_ANON_TRANSPORT_FAILED)
        return 0;
    int valid =
        (transport_state == ARGUS_ANON_TRANSPORT_OFFLINE &&
         next_state == ARGUS_ANON_TRANSPORT_BOOTSTRAPPING) ||
        (transport_state == ARGUS_ANON_TRANSPORT_BOOTSTRAPPING &&
         (next_state == ARGUS_ANON_TRANSPORT_READY ||
          next_state == ARGUS_ANON_TRANSPORT_FAILED ||
          next_state == ARGUS_ANON_TRANSPORT_OFFLINE)) ||
        (transport_state == ARGUS_ANON_TRANSPORT_READY &&
         (next_state == ARGUS_ANON_TRANSPORT_FAILED ||
          next_state == ARGUS_ANON_TRANSPORT_OFFLINE)) ||
        (transport_state == ARGUS_ANON_TRANSPORT_FAILED &&
         (next_state == ARGUS_ANON_TRANSPORT_BOOTSTRAPPING ||
          next_state == ARGUS_ANON_TRANSPORT_OFFLINE));
    if (!valid) return 0;
    transport_state = next_state;
    return 1;
}

int anonymity_connection_allowed(
    argus_security_role_t role,
    argus_capability_type_t type,
    uint16_t rights
) {
    return transport_state == ARGUS_ANON_TRANSPORT_READY &&
        type == ARGUS_CAPABILITY_ANONYMOUS_STREAM &&
        anonymity_capability_allowed(role, type, rights);
}

argus_anonymity_transport_state_t anonymity_transport_state(void) {
    return transport_state;
}

const char *anonymity_transport_state_name(void) {
    if (transport_state == ARGUS_ANON_TRANSPORT_BOOTSTRAPPING)
        return "bootstrapping";
    if (transport_state == ARGUS_ANON_TRANSPORT_READY) return "ready";
    if (transport_state == ARGUS_ANON_TRANSPORT_FAILED) return "failed";
    return "offline";
}

int anonymity_clearnet_allowed(void) { return 0; }
int anonymity_local_dns_allowed(void) { return 0; }

int anonymity_policy_self_test(void) {
    anonymity_policy_init();
    if (anonymity_clearnet_allowed() || anonymity_local_dns_allowed() ||
        anonymity_capability_allowed(
            ARGUS_SECURITY_ROLE_UTILITY,
            ARGUS_CAPABILITY_RAW_NETWORK,
            ARGUS_CAP_RIGHT_CONNECT) ||
        anonymity_capability_allowed(
            ARGUS_SECURITY_ROLE_RENDERER,
            ARGUS_CAPABILITY_ANONYMOUS_STREAM,
            ARGUS_CAP_RIGHT_CONNECT) ||
        !anonymity_capability_allowed(
            ARGUS_SECURITY_ROLE_TOR_TRANSPORT,
            ARGUS_CAPABILITY_RAW_NETWORK,
            ARGUS_CAP_RIGHT_CONNECT) ||
        !anonymity_capability_allowed(
            ARGUS_SECURITY_ROLE_BROWSER_NETWORK,
            ARGUS_CAPABILITY_ANONYMOUS_STREAM,
            ARGUS_CAP_RIGHT_CONNECT) ||
        anonymity_connection_allowed(
            ARGUS_SECURITY_ROLE_BROWSER_NETWORK,
            ARGUS_CAPABILITY_ANONYMOUS_STREAM,
            ARGUS_CAP_RIGHT_CONNECT) ||
        anonymity_transport_update(
            ARGUS_SECURITY_ROLE_BROWSER_UI,
            ARGUS_ANON_TRANSPORT_BOOTSTRAPPING,
            1) ||
        anonymity_transport_update(
            ARGUS_SECURITY_ROLE_TOR_TRANSPORT,
            ARGUS_ANON_TRANSPORT_READY,
            1) ||
        !anonymity_transport_update(
            ARGUS_SECURITY_ROLE_TOR_TRANSPORT,
            ARGUS_ANON_TRANSPORT_BOOTSTRAPPING,
            1) ||
        !anonymity_transport_update(
            ARGUS_SECURITY_ROLE_TOR_TRANSPORT,
            ARGUS_ANON_TRANSPORT_READY,
            1) ||
        !anonymity_connection_allowed(
            ARGUS_SECURITY_ROLE_BROWSER_NETWORK,
            ARGUS_CAPABILITY_ANONYMOUS_STREAM,
            ARGUS_CAP_RIGHT_CONNECT) ||
        anonymity_connection_allowed(
            ARGUS_SECURITY_ROLE_BROWSER_UI,
            ARGUS_CAPABILITY_ANONYMOUS_STREAM,
            ARGUS_CAP_RIGHT_CONNECT) ||
        !anonymity_transport_update(
            ARGUS_SECURITY_ROLE_TOR_TRANSPORT,
            ARGUS_ANON_TRANSPORT_FAILED,
            1) ||
        anonymity_connection_allowed(
            ARGUS_SECURITY_ROLE_BROWSER_NETWORK,
            ARGUS_CAPABILITY_ANONYMOUS_STREAM,
            ARGUS_CAP_RIGHT_CONNECT))
        return 0;
    anonymity_policy_init();
    return transport_state == ARGUS_ANON_TRANSPORT_OFFLINE;
}
