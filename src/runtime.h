#ifndef RUNTIME_H
#define RUNTIME_H

#include "common.h"
#include "net.h"
#include "ast.h"

typedef struct {
    char name[MAX_TAG_NAME_LEN];
    Endpoint endpoint;
    bool is_empty;
    bool in_use;
} TagEntry;

typedef struct {
    TagEntry entries[MAX_TAGS];
    size_t count;
} TagRegistry;

typedef struct {
    char sub_tag[MAX_TAG_NAME_LEN];
    Endpoint from_ep;
    Endpoint to_ep;
    bool in_use;
} RouteEntry;

typedef void (*RuntimePacketHook)(uint32_t seq, const char *packet_type, const char *from_tag, const char *to_tag,
                                  const char *from_ep, const char *to_ep, const char *payload, size_t bytes, void *userdata);

typedef void (*RuntimeRoutingHook)(const char *action, const char *registry, const char *tag, const char *endpoint, void *userdata);

typedef void (*RuntimeStepHook)(size_t step_idx, size_t total_steps, int line, const char *stmt_type, void *userdata);

typedef struct {
    TagRegistry publisher_tags;
    TagRegistry subscriber_tags;
    RouteEntry routes[MAX_ROUTES];
    size_t route_count;
    uint32_t message_sequence;

    // Hooks for Web/Visualizer
    RuntimePacketHook packet_hook;
    RuntimeRoutingHook routing_hook;
    RuntimeStepHook step_hook;
    void *hook_userdata;
    int step_delay_ms;
    volatile bool abort_execution;
    volatile bool is_paused;
} Runtime;

void runtime_init(Runtime *rt);
void runtime_free(Runtime *rt);

bool runtime_set_tag(Runtime *rt, RegistryType reg, const char *name, const Endpoint *ep, bool is_empty);
bool runtime_delete_tag(Runtime *rt, RegistryType reg, const char *name);
TagEntry *runtime_find_tag(Runtime *rt, RegistryType reg, const char *name);

bool runtime_execute_program(Runtime *rt, const Program *prog);
bool runtime_execute_statement(Runtime *rt, const AstStmt *stmt);

bool runtime_send_direct(Runtime *rt, const char *from_tag, const char *to_tag, const char *payload);
void runtime_to_json(const Runtime *rt, char *out_json, size_t max_len);

void runtime_dump_state(const Runtime *rt);

#endif // RUNTIME_H
