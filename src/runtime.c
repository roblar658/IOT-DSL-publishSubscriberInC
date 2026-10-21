#include "runtime.h"
#include "logger.h"

void runtime_init(Runtime *rt) {
    if (!rt) return;
    memset(rt, 0, sizeof(Runtime));
    net_init();
    LOG_DEBUG("RUNTIME", "Runtime initialized");
}

void runtime_free(Runtime *rt) {
    if (!rt) return;
    net_cleanup();
    LOG_DEBUG("RUNTIME", "Runtime finalized");
}

static TagRegistry *get_registry(Runtime *rt, RegistryType reg) {
    return (reg == REGISTRY_PUBLISHER) ? &rt->publisher_tags : &rt->subscriber_tags;
}

TagEntry *runtime_find_tag(Runtime *rt, RegistryType reg, const char *name) {
    if (!rt || !name) return NULL;
    TagRegistry *registry = get_registry(rt, reg);

    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (registry->entries[i].in_use && strcmp(registry->entries[i].name, name) == 0) {
            return &registry->entries[i];
        }
    }
    return NULL;
}

static void safe_copy_tag_name(char *dest, const char *src, size_t max_len) {
    if (!dest || max_len == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t i = 0;
    while (src[i] && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

bool runtime_set_tag(Runtime *rt, RegistryType reg, const char *name, const Endpoint *ep, bool is_empty) {
    if (!rt || !name || strlen(name) == 0) return false;
    TagRegistry *registry = get_registry(rt, reg);

    TagEntry *existing = runtime_find_tag(rt, reg, name);
    if (existing) {
        if (ep) {
            existing->endpoint = *ep;
        }
        existing->is_empty = is_empty || (ep && ep->is_empty);
        existing->endpoint.is_empty = existing->is_empty;

        char ep_str[64];
        net_format_endpoint(&existing->endpoint, ep_str, sizeof(ep_str));
        LOG_ROUTING("ROUTING", "Updated %s tag '%s' -> %s",
                    registry_type_name(reg), name, ep_str);
        if (rt->routing_hook) {
            rt->routing_hook("UPDATE", registry_type_name(reg), name, ep_str, rt->hook_userdata);
        }
        return true;
    }

    // Find free slot
    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (!registry->entries[i].in_use) {
            TagEntry *entry = &registry->entries[i];
            entry->in_use = true;
            safe_copy_tag_name(entry->name, name, sizeof(entry->name));
            if (ep) {
                entry->endpoint = *ep;
            } else {
                memset(&entry->endpoint, 0, sizeof(Endpoint));
            }
            entry->is_empty = is_empty || (ep && ep->is_empty);
            entry->endpoint.is_empty = entry->is_empty;
            if (entry->is_empty) {
                strcpy(entry->endpoint.host, "EMPTY");
                entry->endpoint.port = 0;
            }
            registry->count++;

            char ep_str[64];
            net_format_endpoint(&entry->endpoint, ep_str, sizeof(ep_str));
            LOG_ROUTING("ROUTING", "Registered %s tag '%s' -> %s",
                        registry_type_name(reg), name, ep_str);
            if (rt->routing_hook) {
                rt->routing_hook("REGISTER", registry_type_name(reg), name, ep_str, rt->hook_userdata);
            }
            return true;
        }
    }

    LOG_ERROR("ROUTING", "Cannot add tag '%s': registry '%s' is full (%d max)",
              name, registry_type_name(reg), MAX_TAGS);
    return false;
}

bool runtime_delete_tag(Runtime *rt, RegistryType reg, const char *name) {
    if (!rt || !name) return false;
    TagEntry *entry = runtime_find_tag(rt, reg, name);
    if (!entry) {
        LOG_WARN("ROUTING", "Cannot delete tag '%s': not found in %s",
                 name, registry_type_name(reg));
        return false;
    }

    entry->in_use = false;
    get_registry(rt, reg)->count--;
    LOG_ROUTING("ROUTING", "Deleted %s tag '%s'", registry_type_name(reg), name);
    if (rt->routing_hook) {
        rt->routing_hook("DELETE", registry_type_name(reg), name, "EMPTY", rt->hook_userdata);
    }
    return true;
}

static void escape_json_str(const char *in, char *out, size_t max_len) {
    if (!in || !out || max_len == 0) return;
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < max_len; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if (c == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

static bool send_tag_message(Runtime *rt, const char *type, const TagEntry *pub, const TagEntry *sub, const char *custom_msg) {
    if (!pub || !sub) return false;

    if (pub->is_empty) {
        LOG_WARN("NET_TX", "Cannot send %s: publisher '%s' is EMPTY", type, pub->name);
        return false;
    }

    if (sub->is_empty) {
        LOG_WARN("NET_TX", "Cannot send %s: subscriber '%s' is EMPTY", type, sub->name);
        return false;
    }

    rt->message_sequence++;

    char escaped_payload[1024];
    const char *payload_text = (custom_msg && *custom_msg) ? custom_msg : "PING";
    escape_json_str(payload_text, escaped_payload, sizeof(escaped_payload));

    char json_packet[MAX_PAYLOAD_LEN];
    snprintf(json_packet, sizeof(json_packet),
             "{\"seq\":%u,\"type\":\"%s\",\"from_tag\":\"%s\",\"to_tag\":\"%s\",\"payload\":\"%s\"}",
             rt->message_sequence, type, pub->name, sub->name, escaped_payload);

    LOG_INFO("NET_TX", "Dispatching %s from [%s] to [%s]", type, pub->name, sub->name);

    char from_str[64], to_str[64];
    net_format_endpoint(&pub->endpoint, from_str, sizeof(from_str));
    net_format_endpoint(&sub->endpoint, to_str, sizeof(to_str));

    if (rt->packet_hook) {
        rt->packet_hook(rt->message_sequence, type, pub->name, sub->name,
                        from_str, to_str, payload_text, strlen(json_packet), rt->hook_userdata);
    }

    return net_send_udp(&pub->endpoint, &sub->endpoint, json_packet, strlen(json_packet));
}

bool runtime_execute_statement(Runtime *rt, const AstStmt *stmt) {
    if (!rt || !stmt) return false;

    switch (stmt->type) {
        case STMT_TAGS_BLOCK: {
            RegistryType reg = stmt->as.tags_block.reg_type;
            LOG_INFO("EXEC", "Processing %s block with %zu tags",
                     registry_type_name(reg), stmt->as.tags_block.item_count);
            for (size_t i = 0; i < stmt->as.tags_block.item_count; i++) {
                const AstTagItem *item = &stmt->as.tags_block.items[i];
                runtime_set_tag(rt, reg, item->tag_name, &item->endpoint, item->endpoint.is_empty);
            }
            return true;
        }

        case STMT_CHANGE: {
            RegistryType reg = stmt->as.change_stmt.reg_type;
            const char *name = stmt->as.change_stmt.tag_name;
            bool is_empty = stmt->as.change_stmt.is_empty;

            if (is_empty) {
                Endpoint empty_ep;
                empty_ep.is_empty = true;
                strcpy(empty_ep.host, "EMPTY");
                empty_ep.port = 0;
                return runtime_set_tag(rt, reg, name, &empty_ep, true);
            } else {
                return runtime_set_tag(rt, reg, name, &stmt->as.change_stmt.to_ep, false);
            }
        }

        case STMT_DELETE: {
            RegistryType reg = stmt->as.delete_stmt.reg_type;
            const char *name = stmt->as.delete_stmt.tag_name;
            return runtime_delete_tag(rt, reg, name);
        }

        case STMT_ADD: {
            RegistryType reg = stmt->as.add_stmt.reg_type;
            const char *name = stmt->as.add_stmt.tag_name;
            bool is_empty = stmt->as.add_stmt.is_empty;

            if (is_empty) {
                Endpoint empty_ep;
                empty_ep.is_empty = true;
                strcpy(empty_ep.host, "EMPTY");
                empty_ep.port = 0;
                return runtime_set_tag(rt, reg, name, &empty_ep, true);
            } else {
                return runtime_set_tag(rt, reg, name, &stmt->as.add_stmt.to_ep, false);
            }
        }

        case STMT_SUBSCRIBE: {
            const char *name = stmt->as.subscribe_stmt.tag_name;
            // Record route
            if (rt->route_count < MAX_ROUTES) {
                RouteEntry *route = &rt->routes[rt->route_count++];
                route->in_use = true;
                safe_copy_tag_name(route->sub_tag, name, sizeof(route->sub_tag));
                route->from_ep = stmt->as.subscribe_stmt.from_ep;
                route->to_ep = stmt->as.subscribe_stmt.to_ep;

                char from_str[64], to_str[64];
                net_format_endpoint(&route->from_ep, from_str, sizeof(from_str));
                net_format_endpoint(&route->to_ep, to_str, sizeof(to_str));
                LOG_ROUTING("ROUTING", "Registered subscription route for '%s': %s -> %s",
                            name, from_str, to_str);
            }

            // Update subscriber tag endpoint to to_ep
            runtime_set_tag(rt, REGISTRY_SUBSCRIBER, name, &stmt->as.subscribe_stmt.to_ep, false);
            return true;
        }

        case STMT_BROADCAST: {
            const char *pub_name = stmt->as.broadcast_stmt.pub_tag;
            TagEntry *pub = runtime_find_tag(rt, REGISTRY_PUBLISHER, pub_name);
            if (!pub) {
                LOG_ERROR("EXEC", "broadcast: publisher tag '%s' not found", pub_name);
                return false;
            }

            TagRegistry *sub_reg = &rt->subscriber_tags;
            LOG_INFO("EXEC", "Initiating broadcast from publisher [%s] to all subscriber tags", pub_name);

            int sent_count = 0;
            for (size_t i = 0; i < MAX_TAGS; i++) {
                if (sub_reg->entries[i].in_use) {
                    TagEntry *sub = &sub_reg->entries[i];
                    if (send_tag_message(rt, "BROADCAST", pub, sub, stmt->as.broadcast_stmt.message)) {
                        sent_count++;
                    }
                }
            }

            LOG_INFO("EXEC", "Broadcast from [%s] completed: %d packet(s) sent", pub_name, sent_count);
            return true;
        }

        case STMT_MULTICAST: {
            const char *pub_name = stmt->as.multicast_stmt.pub_tag;
            TagEntry *pub = runtime_find_tag(rt, REGISTRY_PUBLISHER, pub_name);
            if (!pub) {
                LOG_ERROR("EXEC", "multicast: publisher tag '%s' not found", pub_name);
                return false;
            }

            LOG_INFO("EXEC", "Initiating multicast from publisher [%s] to %zu specific subscriber tag(s)",
                     pub_name, stmt->as.multicast_stmt.sub_tag_count);

            int sent_count = 0;
            for (size_t i = 0; i < stmt->as.multicast_stmt.sub_tag_count; i++) {
                const char *sub_name = stmt->as.multicast_stmt.sub_tags[i];
                TagEntry *sub = runtime_find_tag(rt, REGISTRY_SUBSCRIBER, sub_name);
                if (!sub) {
                    LOG_WARN("EXEC", "multicast: subscriber tag '%s' not found in subscriber_tags", sub_name);
                    continue;
                }
                if (send_tag_message(rt, "MULTICAST", pub, sub, stmt->as.multicast_stmt.message)) {
                    sent_count++;
                }
            }

            LOG_INFO("EXEC", "Multicast from [%s] completed: %d packet(s) sent", pub_name, sent_count);
            return true;
        }

        case STMT_SEND: {
            const char *pub_name = stmt->as.send_stmt.pub_tag;
            const char *sub_name = stmt->as.send_stmt.sub_tag;

            TagEntry *pub = runtime_find_tag(rt, REGISTRY_PUBLISHER, pub_name);
            if (!pub) {
                LOG_ERROR("EXEC", "send: publisher tag '%s' not found", pub_name);
                return false;
            }

            TagEntry *sub = runtime_find_tag(rt, REGISTRY_SUBSCRIBER, sub_name);
            if (!sub) {
                LOG_ERROR("EXEC", "send: subscriber tag '%s' not found", sub_name);
                return false;
            }

            LOG_INFO("EXEC", "Direct send from publisher [%s] to subscriber [%s]", pub_name, sub_name);
            return send_tag_message(rt, "SEND", pub, sub, stmt->as.send_stmt.message);
        }

        case STMT_SUBSCRIBER_BLOCK: {
            const char *block_tag = stmt->as.subscriber_block.tag_name;
            LOG_INFO("EXEC", "Executing subscribers[%s] action list (%zu statements)",
                     block_tag, stmt->as.subscriber_block.inner_count);

            for (size_t i = 0; i < stmt->as.subscriber_block.inner_count; i++) {
                runtime_execute_statement(rt, stmt->as.subscriber_block.inner_stmts[i]);
            }
            return true;
        }

        case STMT_SLEEP: {
            int ms = stmt->as.sleep_stmt.ms;
            LOG_INFO("EXEC", "Sleeping for %d ms...", ms);
            sleep_ms(ms);
            return true;
        }

        case STMT_LOG: {
            LOG_INFO("SCRIPT", "%s", stmt->as.log_stmt.message ? stmt->as.log_stmt.message : "");
            return true;
        }

        default:
            LOG_WARN("EXEC", "Unknown statement type %d at line %d", stmt->type, stmt->line);
            return false;
    }
}

bool runtime_execute_program(Runtime *rt, const Program *prog) {
    if (!rt || !prog) return false;

    rt->abort_execution = false;
    rt->is_paused = false;

    LOG_INFO("EXEC", "Starting program execution (%zu statements)...", prog->count);

    for (size_t i = 0; i < prog->count; i++) {
        if (rt->abort_execution) {
            LOG_WARN("EXEC", "Program execution aborted by user");
            break;
        }

        while (rt->is_paused && !rt->abort_execution) {
            sleep_ms(50);
        }

        if (rt->step_hook) {
            rt->step_hook(i, prog->count, prog->statements[i]->line,
                          ast_stmt_type_name(prog->statements[i]->type), rt->hook_userdata);
        }

        runtime_execute_statement(rt, prog->statements[i]);

        if (rt->step_delay_ms > 0) {
            sleep_ms(rt->step_delay_ms);
        }
    }

    LOG_INFO("EXEC", "Program execution finished. Final routing state below:");
    runtime_dump_state(rt);
    return true;
}

bool runtime_send_direct(Runtime *rt, const char *from_tag, const char *to_tag, const char *payload) {
    if (!rt || !from_tag || !to_tag) return false;
    TagEntry *pub = runtime_find_tag(rt, REGISTRY_PUBLISHER, from_tag);
    if (!pub) {
        Endpoint ep;
        net_parse_endpoint("10.0.1.10:8100", &ep);
        runtime_set_tag(rt, REGISTRY_PUBLISHER, from_tag, &ep, false);
        pub = runtime_find_tag(rt, REGISTRY_PUBLISHER, from_tag);
    }
    TagEntry *sub = runtime_find_tag(rt, REGISTRY_SUBSCRIBER, to_tag);
    if (!sub) {
        Endpoint ep;
        net_parse_endpoint("10.0.99.1:9199", &ep);
        runtime_set_tag(rt, REGISTRY_SUBSCRIBER, to_tag, &ep, false);
        sub = runtime_find_tag(rt, REGISTRY_SUBSCRIBER, to_tag);
    }
    LOG_INFO("EXEC", "Direct send from publisher [%s] to subscriber [%s]", from_tag, to_tag);
    return send_tag_message(rt, "SEND", pub, sub, payload);
}

bool runtime_send_broadcast(Runtime *rt, const char *from_tag, const char *payload) {
    if (!rt || !from_tag) return false;
    TagEntry *pub = runtime_find_tag(rt, REGISTRY_PUBLISHER, from_tag);
    if (!pub) {
        Endpoint ep;
        net_parse_endpoint("10.0.99.1:8199", &ep);
        runtime_set_tag(rt, REGISTRY_PUBLISHER, from_tag, &ep, false);
        pub = runtime_find_tag(rt, REGISTRY_PUBLISHER, from_tag);
    }
    TagRegistry *sub_reg = &rt->subscriber_tags;
    LOG_INFO("EXEC", "Initiating broadcast from [%s] to all subscribers", from_tag);

    int sent_count = 0;
    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (sub_reg->entries[i].in_use && !sub_reg->entries[i].is_empty) {
            TagEntry *sub = &sub_reg->entries[i];
            if (send_tag_message(rt, "BROADCAST", pub, sub, payload)) {
                sent_count++;
            }
        }
    }
    LOG_INFO("EXEC", "Broadcast from [%s] completed: %d packet(s) sent", from_tag, sent_count);
    return sent_count > 0;
}

void runtime_to_json(const Runtime *rt, char *out_json, size_t max_len) {
    if (!rt || !out_json || max_len == 0) return;
    size_t o = 0;
    o += snprintf(out_json + o, max_len - o, "{\"publishers\":[");

    bool first = true;
    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (rt->publisher_tags.entries[i].in_use) {
            const TagEntry *e = &rt->publisher_tags.entries[i];
            char ep_str[64];
            net_format_endpoint(&e->endpoint, ep_str, sizeof(ep_str));
            o += snprintf(out_json + o, max_len - o, "%s{\"tag\":\"%s\",\"endpoint\":\"%s\",\"empty\":%s}",
                          first ? "" : ",", e->name, ep_str, e->is_empty ? "true" : "false");
            first = false;
        }
    }

    o += snprintf(out_json + o, max_len - o, "],\"subscribers\":[");
    first = true;
    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (rt->subscriber_tags.entries[i].in_use) {
            const TagEntry *e = &rt->subscriber_tags.entries[i];
            char ep_str[64];
            net_format_endpoint(&e->endpoint, ep_str, sizeof(ep_str));
            o += snprintf(out_json + o, max_len - o, "%s{\"tag\":\"%s\",\"endpoint\":\"%s\",\"empty\":%s}",
                          first ? "" : ",", e->name, ep_str, e->is_empty ? "true" : "false");
            first = false;
        }
    }

    o += snprintf(out_json + o, max_len - o, "],\"routes\":[");
    first = true;
    for (size_t i = 0; i < rt->route_count; i++) {
        const RouteEntry *r = &rt->routes[i];
        if (r->in_use) {
            char from_str[64], to_str[64];
            net_format_endpoint(&r->from_ep, from_str, sizeof(from_str));
            net_format_endpoint(&r->to_ep, to_str, sizeof(to_str));
            o += snprintf(out_json + o, max_len - o, "%s{\"subscriber\":\"%s\",\"from\":\"%s\",\"to\":\"%s\"}",
                          first ? "" : ",", r->sub_tag, from_str, to_str);
            first = false;
        }
    }
    snprintf(out_json + o, max_len - o, "]}");
}

void runtime_dump_state(const Runtime *rt) {
    if (!rt) return;

    printf("\n==================== ROUTING TABLE DUMP ====================\n");

    printf("\n[PUBLISHER TAGS] (count: %zu)\n", rt->publisher_tags.count);
    printf("  %-16s %-24s %-8s\n", "TAG NAME", "ENDPOINT", "STATE");
    printf("  --------------------------------------------------\n");
    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (rt->publisher_tags.entries[i].in_use) {
            const TagEntry *e = &rt->publisher_tags.entries[i];
            char ep_str[64];
            net_format_endpoint(&e->endpoint, ep_str, sizeof(ep_str));
            printf("  %-16s %-24s %-8s\n", e->name, ep_str, e->is_empty ? "EMPTY" : "ACTIVE");
        }
    }

    printf("\n[SUBSCRIBER TAGS] (count: %zu)\n", rt->subscriber_tags.count);
    printf("  %-16s %-24s %-8s\n", "TAG NAME", "ENDPOINT", "STATE");
    printf("  --------------------------------------------------\n");
    for (size_t i = 0; i < MAX_TAGS; i++) {
        if (rt->subscriber_tags.entries[i].in_use) {
            const TagEntry *e = &rt->subscriber_tags.entries[i];
            char ep_str[64];
            net_format_endpoint(&e->endpoint, ep_str, sizeof(ep_str));
            printf("  %-16s %-24s %-8s\n", e->name, ep_str, e->is_empty ? "EMPTY" : "ACTIVE");
        }
    }

    if (rt->route_count > 0) {
        printf("\n[SUBSCRIPTION ROUTES] (count: %zu)\n", rt->route_count);
        printf("  %-16s %-22s -> %-22s\n", "SUBSCRIBER TAG", "FROM ENDPOINT", "TO ENDPOINT");
        printf("  ---------------------------------------------------------------\n");
        for (size_t i = 0; i < rt->route_count; i++) {
            const RouteEntry *r = &rt->routes[i];
            char from_str[64], to_str[64];
            net_format_endpoint(&r->from_ep, from_str, sizeof(from_str));
            net_format_endpoint(&r->to_ep, to_str, sizeof(to_str));
            printf("  %-16s %-22s -> %-22s\n", r->sub_tag, from_str, to_str);
        }
    }

    printf("============================================================\n\n");
    fflush(stdout);
}
