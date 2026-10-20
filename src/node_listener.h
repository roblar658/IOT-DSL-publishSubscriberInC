#ifndef NODE_LISTENER_H
#define NODE_LISTENER_H

#include "common.h"
#include "net.h"

typedef struct {
    char tag_name[MAX_TAG_NAME_LEN];
    char bind_host[MAX_HOST_LEN];
    int port;
    int max_packets;       // 0 for infinite
    int received_count;
    const char *logfile;
} NodeListenerConfig;

int run_node_listener(const NodeListenerConfig *config);

#endif // NODE_LISTENER_H
