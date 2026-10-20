#include "net.h"
#include "logger.h"

static bool s_net_initialized = false;

bool net_init(void) {
    if (s_net_initialized) return true;
#if defined(PLATFORM_WINDOWS)
    WSADATA wsa;
    int res = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (res != 0) {
        LOG_ERROR("NET", "WSAStartup failed with error: %d", res);
        return false;
    }
#endif
    s_net_initialized = true;
    LOG_DEBUG("NET", "Network subsystem initialized");
    return true;
}

void net_cleanup(void) {
    if (!s_net_initialized) return;
#if defined(PLATFORM_WINDOWS)
    WSACleanup();
#endif
    s_net_initialized = false;
    LOG_DEBUG("NET", "Network subsystem cleaned up");
}

static void safe_copy_str(char *dest, const char *src, size_t max_dest) {
    if (!dest || max_dest == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t i = 0;
    while (src[i] && i + 1 < max_dest) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

bool net_parse_endpoint(const char *str, Endpoint *out_ep) {
    if (!str || !out_ep) return false;
    memset(out_ep, 0, sizeof(Endpoint));

    char buf[256];
    safe_copy_str(buf, str, sizeof(buf));
    trim_whitespace(buf);

    if (strcmp(buf, "EMPTY") == 0 || strlen(buf) == 0) {
        out_ep->is_empty = true;
        strcpy(out_ep->host, "EMPTY");
        out_ep->port = 0;
        return true;
    }

    out_ep->is_empty = false;

    // Check for "::" delimiter first (e.g. LAMPE::ip::port or ip::port)
    char *delim = strstr(buf, "::");
    if (delim) {
        *delim = '\0';
        char *port_str = delim + 2;
        safe_copy_str(out_ep->host, buf, sizeof(out_ep->host));
        out_ep->port = atoi(port_str);
    } else {
        // Single colon delimiter (e.g. ip:port)
        char *colon = strrchr(buf, ':');
        if (colon) {
            *colon = '\0';
            char *port_str = colon + 1;
            safe_copy_str(out_ep->host, buf, sizeof(out_ep->host));
            out_ep->port = atoi(port_str);
        } else {
            // No colon: could be just a port number e.g. "9001"
            out_ep->port = atoi(buf);
            if (out_ep->port > 0) {
                strcpy(out_ep->host, "0.0.0.0");
            } else {
                safe_copy_str(out_ep->host, buf, sizeof(out_ep->host));
                out_ep->port = 0;
            }
        }
    }

    trim_whitespace(out_ep->host);
    if (strlen(out_ep->host) == 0) {
        strcpy(out_ep->host, "127.0.0.1");
    }

    return true;
}

void net_format_endpoint(const Endpoint *ep, char *buf, size_t max_size) {
    if (!ep || !buf || max_size == 0) return;
    if (ep->is_empty) {
        snprintf(buf, max_size, "EMPTY");
    } else {
        snprintf(buf, max_size, "%s:%d", ep->host, ep->port);
    }
}

bool net_endpoints_equal(const Endpoint *a, const Endpoint *b) {
    if (!a || !b) return false;
    if (a->is_empty && b->is_empty) return true;
    if (a->is_empty != b->is_empty) return false;
    if (a->port != b->port) return false;
    return (strcmp(a->host, b->host) == 0);
}

bool net_send_udp(const Endpoint *from_ep, const Endpoint *to_ep, const char *payload, size_t len) {
    if (!net_init()) return false;

    char to_str[160], from_str[160];
    net_format_endpoint(to_ep, to_str, sizeof(to_str));
    if (from_ep) {
        net_format_endpoint(from_ep, from_str, sizeof(from_str));
    } else {
        strcpy(from_str, "0.0.0.0:ephemeral");
    }

    if (!to_ep || to_ep->is_empty) {
        LOG_WARN("NET_TX", "Cannot send to EMPTY destination (%s)", to_str);
        return false;
    }

    if (to_ep->port <= 0) {
        LOG_WARN("NET_TX", "Cannot send to destination with invalid port (%s)", to_str);
        return false;
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", to_ep->port);

    int err = getaddrinfo(to_ep->host, port_str, &hints, &res);
    if (err != 0 || !res) {
        LOG_ERROR("NET_TX", "Failed to resolve destination '%s': %s", to_ep->host, gai_strerror(err));
        return false;
    }

#if defined(PLATFORM_WINDOWS)
    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("NET_TX", "socket() failed with error: %d", WSAGetLastError());
        freeaddrinfo(res);
        return false;
    }
#else
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        LOG_ERROR("NET_TX", "socket() failed: %s", strerror(errno));
        freeaddrinfo(res);
        return false;
    }
#endif

    // Optional bind to local from_ep port if requested
    if (from_ep && !from_ep->is_empty && from_ep->port > 0) {
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons((uint16_t)from_ep->port);
        local_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
            LOG_DEBUG("NET_TX", "Could not bind to source port %d (might be bound by listener); using ephemeral port", from_ep->port);
        }
    }

    int bytes_sent = sendto(sock, payload, (int)len, 0, res->ai_addr, (int)res->ai_addrlen);
    bool success = true;

    if (bytes_sent < 0) {
#if defined(PLATFORM_WINDOWS)
        LOG_ERROR("NET_TX", "sendto(%s -> %s) failed with error: %d", from_str, to_str, WSAGetLastError());
#else
        LOG_ERROR("NET_TX", "sendto(%s -> %s) failed: %s", from_str, to_str, strerror(errno));
#endif
        success = false;
    } else {
        LOG_NETWORK("NET_TX", "Sent %d bytes: %s -> %s | Payload: '%s'",
                    bytes_sent, from_str, to_str, payload ? payload : "");
    }

#if defined(PLATFORM_WINDOWS)
    closesocket(sock);
#else
    close(sock);
#endif
    freeaddrinfo(res);

    return success;
}

bool net_listen_udp(const char *bind_host, int port, NetPacketHandler handler, void *user_data, volatile bool *stop_flag) {
    if (!net_init()) return false;

    if (port <= 0) {
        LOG_ERROR("NET_RX", "Invalid port for listener: %d", port);
        return false;
    }

#if defined(PLATFORM_WINDOWS)
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("NET_RX", "socket() failed: %d", WSAGetLastError());
        return false;
    }
#else
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERROR("NET_RX", "socket() failed: %s", strerror(errno));
        return false;
    }
#endif

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // Set receive timeout so loop can check stop_flag
#if defined(PLATFORM_WINDOWS)
    DWORD timeout_ms = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000; // 500ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)port);

    if (bind_host && strlen(bind_host) > 0 && strcmp(bind_host, "0.0.0.0") != 0) {
        inet_pton(AF_INET, bind_host, &bind_addr.sin_addr);
    } else {
        bind_addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
#if defined(PLATFORM_WINDOWS)
        LOG_ERROR("NET_RX", "bind() to %s:%d failed with error: %d",
                  (bind_host ? bind_host : "0.0.0.0"), port, WSAGetLastError());
        closesocket(sock);
#else
        LOG_ERROR("NET_RX", "bind() to %s:%d failed: %s",
                  (bind_host ? bind_host : "0.0.0.0"), port, strerror(errno));
        close(sock);
#endif
        return false;
    }

    LOG_INFO("NET_RX", "UDP Listener bound and listening on %s:%d",
             (bind_host ? bind_host : "0.0.0.0"), port);

    char rx_buffer[MAX_PAYLOAD_LEN + 1];

    while (!stop_flag || !(*stop_flag)) {
        struct sockaddr_in sender_addr;
#if defined(PLATFORM_WINDOWS)
        int addr_len = sizeof(sender_addr);
#else
        socklen_t addr_len = sizeof(sender_addr);
#endif
        int bytes = recvfrom(sock, rx_buffer, MAX_PAYLOAD_LEN, 0,
                             (struct sockaddr *)&sender_addr, &addr_len);

        if (bytes > 0) {
            rx_buffer[bytes] = '\0';
            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));
            int sender_port = ntohs(sender_addr.sin_port);

            LOG_NETWORK("NET_RX", "Received %d bytes from %s:%d | Data: '%s'",
                        bytes, sender_ip, sender_port, rx_buffer);

            if (handler) {
                handler(sender_ip, sender_port, rx_buffer, (size_t)bytes, user_data);
            }
        }
    }

#if defined(PLATFORM_WINDOWS)
    closesocket(sock);
#else
    close(sock);
#endif

    LOG_INFO("NET_RX", "UDP Listener on port %d stopped", port);
    return true;
}
