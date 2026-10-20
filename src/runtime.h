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

typedef struct {
    TagRegistry publisher_tags;
    TagRegistry subscriber_tags;
    RouteEntry routes[MAX_ROUTES];
    size_t route_count;
    uint32_t message_sequence;
} Runtime;

void runtime_init(Runtime *rt);
void runtime_free(Runtime *rt);

bool runtime_set_tag(Runtime *rt, RegistryType reg, const char *name, const Endpoint *ep, bool is_empty);
bool runtime_delete_tag(Runtime *rt, RegistryType reg, const char *name);
TagEntry *runtime_find_tag(Runtime *rt, RegistryType reg, const char *name);

bool runtime_execute_program(Runtime *rt, const Program *prog);
bool runtime_execute_statement(Runtime *rt, const AstStmt *stmt);

void runtime_dump_state(const Runtime *rt);

#endif // RUNTIME_H
