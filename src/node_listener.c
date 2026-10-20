#include "node_listener.h"
#include "logger.h"
#include <signal.h>

static volatile bool s_listener_stop = false;

static void handle_sigint(int sig) {
    (void)sig;
    s_listener_stop = true;
    LOG_INFO("LISTENER", "Interrupted by signal, stopping listener...");
}

typedef struct {
    NodeListenerConfig config;
    int packet_counter;
} ListenerContext;

static void on_packet_received(const char *sender_ip, int sender_port, const char *data, size_t len, void *user_data) {
    ListenerContext *ctx = (ListenerContext *)user_data;
    ctx->packet_counter++;

    LOG_INFO("NET_RX", ">>> [NODE %s:%d] Packet #%d received from %s:%d (%zu bytes)",
             ctx->config.tag_name, ctx->config.port, ctx->packet_counter, sender_ip, sender_port, len);
    LOG_INFO("NET_RX", "    Raw data: %s", data);

    // Simple JSON field extraction for display
    char type[32] = "UNKNOWN";
    char from_tag[64] = "UNKNOWN";
    char to_tag[64] = "UNKNOWN";
    char payload[512] = "";

    const char *p_type = strstr(data, "\"type\":\"");
    if (p_type) {
        sscanf(p_type + 8, "%31[^\"]", type);
    }
    const char *p_from = strstr(data, "\"from_tag\":\"");
    if (p_from) {
        sscanf(p_from + 12, "%63[^\"]", from_tag);
    }
    const char *p_to = strstr(data, "\"to_tag\":\"");
    if (p_to) {
        sscanf(p_to + 10, "%63[^\"]", to_tag);
    }
    const char *p_payload = strstr(data, "\"payload\":\"");
    if (p_payload) {
        sscanf(p_payload + 11, "%511[^\"]", payload);
    }

    LOG_NETWORK("NODE_EVENT", "Node '%s' processed %s from '%s' | Content: '%s'",
                ctx->config.tag_name, type, from_tag, payload);

    if (ctx->config.max_packets > 0 && ctx->packet_counter >= ctx->config.max_packets) {
        LOG_INFO("LISTENER", "Reached max packet limit (%d). Shutting down listener.", ctx->config.max_packets);
        s_listener_stop = true;
    }
}

int run_node_listener(const NodeListenerConfig *config) {
    if (!config) return 1;

    s_listener_stop = false;
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    char log_filename[256];
    if (config->logfile && strlen(config->logfile) > 0) {
        strncpy(log_filename, config->logfile, sizeof(log_filename) - 1);
    } else {
        snprintf(log_filename, sizeof(log_filename), "node_%s.log", config->tag_name);
    }

    logger_init(log_filename, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);

    LOG_INFO("LISTENER", "Starting node listener daemon for tag '%s' on %s:%d...",
             config->tag_name, (config->bind_host[0] ? config->bind_host : "0.0.0.0"), config->port);
    LOG_INFO("LISTENER", "Logs will be recorded in '%s'", log_filename);

    ListenerContext ctx;
    ctx.config = *config;
    ctx.packet_counter = 0;

    const char *host = config->bind_host[0] ? config->bind_host : "0.0.0.0";
    bool ok = net_listen_udp(host, config->port, on_packet_received, &ctx, &s_listener_stop);

    LOG_INFO("LISTENER", "Node listener for tag '%s' stopped. Total packets processed: %d",
             config->tag_name, ctx.packet_counter);

    logger_close();
    return ok ? 0 : 1;
}
