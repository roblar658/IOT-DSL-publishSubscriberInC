#ifndef NET_H
#define NET_H

#include "common.h"

typedef struct {
    char host[MAX_HOST_LEN];
    int port;
    bool is_empty;
} Endpoint;

bool net_init(void);
void net_cleanup(void);

bool net_parse_endpoint(const char *str, Endpoint *out_ep);
void net_format_endpoint(const Endpoint *ep, char *buf, size_t max_size);
bool net_endpoints_equal(const Endpoint *a, const Endpoint *b);

bool net_send_udp(const Endpoint *from_ep, const Endpoint *to_ep, const char *payload, size_t len);

typedef void (*NetPacketHandler)(const char *sender_ip, int sender_port, const char *data, size_t len, void *user_data);

bool net_listen_udp(const char *bind_host, int port, NetPacketHandler handler, void *user_data, volatile bool *stop_flag);

#endif // NET_H
