#include "parser.h"
#include "logger.h"

static void safe_copy_str(char *dest, const char *src, size_t max_dest) {
    if (!dest || max_dest == 0) return;
    if (!src) { dest[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i + 1 < max_dest) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void parser_advance(Parser *parser) {
    parser->current = lexer_next_token(&parser->lexer);
}

static bool check(Parser *parser, DslTokenType type) {
    return parser->current.type == type;
}

static bool match(Parser *parser, DslTokenType type) {
    if (check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    return false;
}

static void set_error(Parser *parser, const char *fmt, ...) {
    if (parser->has_error) return; // keep first error
    parser->has_error = true;
    va_list args;
    va_start(args, fmt);
    vsnprintf(parser->error_msg, sizeof(parser->error_msg), fmt, args);
    va_end(args);
    LOG_ERROR("PARSER", "Line %d:%d: %s (current token: '%s')",
              parser->current.line, parser->current.col, parser->error_msg, parser->current.text);
}

static bool expect(Parser *parser, DslTokenType type, const char *msg) {
    if (check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    set_error(parser, "Expected %s, got '%s'. %s", token_type_name(type), parser->current.text, msg ? msg : "");
    return false;
}

void parser_init(Parser *parser, const char *source) {
    lexer_init(&parser->lexer, source);
    parser->has_error = false;
    parser->error_msg[0] = '\0';
    parser_advance(parser);
}

// Helper to parse endpoint: EMPTY | host:port | host::port | "host:port" | ident:number
static bool parse_endpoint(Parser *parser, Endpoint *out_ep) {
    if (match(parser, TOKEN_KW_EMPTY)) {
        out_ep->is_empty = true;
        strcpy(out_ep->host, "EMPTY");
        out_ep->port = 0;
        return true;
    }

    if (check(parser, TOKEN_ENDPOINT) || check(parser, TOKEN_STRING)) {
        bool ok = net_parse_endpoint(parser->current.text, out_ep);
        parser_advance(parser);
        return ok;
    }

    if (check(parser, TOKEN_IDENTIFIER) || check(parser, TOKEN_NUMBER)) {
        char host_buf[128];
        safe_copy_str(host_buf, parser->current.text, sizeof(host_buf));
        parser_advance(parser);

        if (match(parser, TOKEN_COLON) || match(parser, TOKEN_COLONCOLON)) {
            if (check(parser, TOKEN_NUMBER) || check(parser, TOKEN_IDENTIFIER)) {
                char combined[512];
                snprintf(combined, sizeof(combined), "%s:%s", host_buf, parser->current.text);
                parser_advance(parser);
                return net_parse_endpoint(combined, out_ep);
            }
        }
        return net_parse_endpoint(host_buf, out_ep);
    }

    set_error(parser, "Expected endpoint (ip:port or EMPTY), got '%s'", parser->current.text);
    return false;
}

// Helper to parse optional message payload: :: "message"
static char *parse_optional_payload(Parser *parser) {
    if (match(parser, TOKEN_COLONCOLON)) {
        if (check(parser, TOKEN_STRING) || check(parser, TOKEN_IDENTIFIER)) {
            char *msg = safe_strdup(parser->current.text);
            parser_advance(parser);
            return msg;
        }
    }
    return NULL;
}

// Parse registry identifier: publisher_tags or subscriber_tags
static bool parse_registry_type(Parser *parser, RegistryType *out_reg) {
    if (match(parser, TOKEN_KW_PUBLISHER_TAGS)) {
        *out_reg = REGISTRY_PUBLISHER;
        return true;
    }
    if (match(parser, TOKEN_KW_SUBSCRIBER_TAGS)) {
        *out_reg = REGISTRY_SUBSCRIBER;
        return true;
    }
    if (check(parser, TOKEN_IDENTIFIER)) {
        if (strcasecmp(parser->current.text, "publisher_tags") == 0) {
            *out_reg = REGISTRY_PUBLISHER;
            parser_advance(parser);
            return true;
        }
        if (strcasecmp(parser->current.text, "subscriber_tags") == 0) {
            *out_reg = REGISTRY_SUBSCRIBER;
            parser_advance(parser);
            return true;
        }
    }
    set_error(parser, "Expected 'publisher_tags' or 'subscriber_tags', got '%s'", parser->current.text);
    return false;
}

// Parse tag block: publisher_tags:{ ... } or subscriber_tags:{ ... }
static AstStmt *parse_tags_block(Parser *parser, RegistryType reg) {
    int line = parser->current.line;
    match(parser, TOKEN_COLON); // optional colon before {

    if (!expect(parser, TOKEN_LBRACE, "Opening brace '{' required for tag block")) {
        return NULL;
    }

    AstStmt *stmt = ast_stmt_create(STMT_TAGS_BLOCK, line);
    stmt->as.tags_block.reg_type = reg;
    stmt->as.tags_block.item_count = 0;
    stmt->as.tags_block.items = NULL;

    size_t cap = 8;
    stmt->as.tags_block.items = (AstTagItem *)malloc(sizeof(AstTagItem) * cap);

    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF) && !parser->has_error) {
        if (check(parser, TOKEN_IDENTIFIER) || check(parser, TOKEN_ENDPOINT)) {
            char tag_name[MAX_TAG_NAME_LEN];
            safe_copy_str(tag_name, parser->current.text, sizeof(tag_name));
            parser_advance(parser);

            Endpoint ep;
            memset(&ep, 0, sizeof(Endpoint));

            if (match(parser, TOKEN_COLONCOLON) || match(parser, TOKEN_COLON)) {
                if (!parse_endpoint(parser, &ep)) {
                    ast_stmt_free(stmt);
                    return NULL;
                }
            } else {
                // No colon provided, default to EMPTY
                ep.is_empty = true;
                strcpy(ep.host, "EMPTY");
            }

            if (stmt->as.tags_block.item_count >= cap) {
                cap *= 2;
                stmt->as.tags_block.items = (AstTagItem *)realloc(stmt->as.tags_block.items, sizeof(AstTagItem) * cap);
            }

            AstTagItem *item = &stmt->as.tags_block.items[stmt->as.tags_block.item_count++];
            safe_copy_str(item->tag_name, tag_name, sizeof(item->tag_name));
            item->endpoint = ep;

            match(parser, TOKEN_COMMA); // optional comma
        } else {
            set_error(parser, "Unexpected token in tag block: '%s'", parser->current.text);
            ast_stmt_free(stmt);
            return NULL;
        }
    }

    expect(parser, TOKEN_RBRACE, "Closing brace '}' required for tag block");
    return stmt;
}

// Forward declaration of single statement parser
static AstStmt *parse_statement(Parser *parser);

// Parse change(...)
static AstStmt *parse_change(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'change'

    if (!expect(parser, TOKEN_LPAREN, "Opening '(' after 'change'")) return NULL;

    RegistryType reg;
    if (!parse_registry_type(parser, &reg)) return NULL;

    if (!expect(parser, TOKEN_COMMA, "Comma ',' between registry and tag")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected tag name identifier, got '%s'", parser->current.text);
        return NULL;
    }
    char tag_name[MAX_TAG_NAME_LEN];
    safe_copy_str(tag_name, parser->current.text, sizeof(tag_name));
    parser_advance(parser);

    if (!expect(parser, TOKEN_RPAREN, "Closing ')' after change arguments")) return NULL;

    if (!expect(parser, TOKEN_COLONCOLON, "Double colon '::' after change(...)")) return NULL;

    AstStmt *stmt = ast_stmt_create(STMT_CHANGE, line);
    stmt->as.change_stmt.reg_type = reg;
    safe_copy_str(stmt->as.change_stmt.tag_name, tag_name, sizeof(stmt->as.change_stmt.tag_name));

    if (match(parser, TOKEN_KW_EMPTY)) {
        stmt->as.change_stmt.is_empty = true;
        stmt->as.change_stmt.to_ep.is_empty = true;
        strcpy(stmt->as.change_stmt.to_ep.host, "EMPTY");
        return stmt;
    }

    // Parse from_ep -> to_ep
    Endpoint ep1;
    if (!parse_endpoint(parser, &ep1)) {
        ast_stmt_free(stmt);
        return NULL;
    }

    if (match(parser, TOKEN_ARROW)) {
        Endpoint ep2;
        if (!parse_endpoint(parser, &ep2)) {
            ast_stmt_free(stmt);
            return NULL;
        }
        stmt->as.change_stmt.from_ep = ep1;
        stmt->as.change_stmt.to_ep = ep2;
        stmt->as.change_stmt.is_empty = false;
    } else {
        stmt->as.change_stmt.from_ep.is_empty = true;
        stmt->as.change_stmt.to_ep = ep1;
        stmt->as.change_stmt.is_empty = false;
    }

    return stmt;
}

// Parse delete(...)
static AstStmt *parse_delete(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'delete'

    if (!expect(parser, TOKEN_LPAREN, "Opening '(' after 'delete'")) return NULL;

    RegistryType reg;
    if (!parse_registry_type(parser, &reg)) return NULL;

    if (!expect(parser, TOKEN_COMMA, "Comma ',' between registry and tag")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected tag name identifier, got '%s'", parser->current.text);
        return NULL;
    }
    char tag_name[MAX_TAG_NAME_LEN];
    safe_copy_str(tag_name, parser->current.text, sizeof(tag_name));
    parser_advance(parser);

    if (!expect(parser, TOKEN_RPAREN, "Closing ')' after delete arguments")) return NULL;

    AstStmt *stmt = ast_stmt_create(STMT_DELETE, line);
    stmt->as.delete_stmt.reg_type = reg;
    safe_copy_str(stmt->as.delete_stmt.tag_name, tag_name, sizeof(stmt->as.delete_stmt.tag_name));
    return stmt;
}

// Parse add(...)
static AstStmt *parse_add(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'add'

    if (!expect(parser, TOKEN_LPAREN, "Opening '(' after 'add'")) return NULL;

    RegistryType reg;
    if (!parse_registry_type(parser, &reg)) return NULL;

    if (!expect(parser, TOKEN_COMMA, "Comma ',' between registry and tag")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected tag name identifier, got '%s'", parser->current.text);
        return NULL;
    }
    char tag_name[MAX_TAG_NAME_LEN];
    safe_copy_str(tag_name, parser->current.text, sizeof(tag_name));
    parser_advance(parser);

    if (!expect(parser, TOKEN_RPAREN, "Closing ')' after add arguments")) return NULL;

    AstStmt *stmt = ast_stmt_create(STMT_ADD, line);
    stmt->as.add_stmt.reg_type = reg;
    safe_copy_str(stmt->as.add_stmt.tag_name, tag_name, sizeof(stmt->as.add_stmt.tag_name));

    // Check if followed by :: from_ip:port -> to_ip:port
    if (match(parser, TOKEN_COLONCOLON)) {
        if (match(parser, TOKEN_KW_EMPTY)) {
            stmt->as.add_stmt.is_empty = true;
            stmt->as.add_stmt.to_ep.is_empty = true;
            strcpy(stmt->as.add_stmt.to_ep.host, "EMPTY");
        } else {
            Endpoint ep1;
            if (!parse_endpoint(parser, &ep1)) {
                ast_stmt_free(stmt);
                return NULL;
            }
            if (match(parser, TOKEN_ARROW)) {
                Endpoint ep2;
                if (!parse_endpoint(parser, &ep2)) {
                    ast_stmt_free(stmt);
                    return NULL;
                }
                stmt->as.add_stmt.from_ep = ep1;
                stmt->as.add_stmt.to_ep = ep2;
                stmt->as.add_stmt.is_empty = false;
            } else {
                stmt->as.add_stmt.from_ep.is_empty = true;
                stmt->as.add_stmt.to_ep = ep1;
                stmt->as.add_stmt.is_empty = false;
            }
        }
    } else {
        // Without :: endpoint specification, medfører tag::EMPTY
        stmt->as.add_stmt.is_empty = true;
        stmt->as.add_stmt.to_ep.is_empty = true;
        strcpy(stmt->as.add_stmt.to_ep.host, "EMPTY");
    }

    return stmt;
}

// Parse subscribe(...)
static AstStmt *parse_subscribe(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'subscribe'

    if (!expect(parser, TOKEN_LPAREN, "Opening '(' after 'subscribe'")) return NULL;

    RegistryType reg;
    if (!parse_registry_type(parser, &reg)) return NULL;

    if (!expect(parser, TOKEN_COMMA, "Comma ',' between registry and tag")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected tag name identifier, got '%s'", parser->current.text);
        return NULL;
    }
    char tag_name[MAX_TAG_NAME_LEN];
    safe_copy_str(tag_name, parser->current.text, sizeof(tag_name));
    parser_advance(parser);

    if (!expect(parser, TOKEN_RPAREN, "Closing ')' after subscribe arguments")) return NULL;

    if (!expect(parser, TOKEN_COLONCOLON, "Double colon '::' after subscribe(...)")) return NULL;

    Endpoint ep1;
    if (!parse_endpoint(parser, &ep1)) return NULL;

    AstStmt *stmt = ast_stmt_create(STMT_SUBSCRIBE, line);
    stmt->as.subscribe_stmt.reg_type = reg;
    safe_copy_str(stmt->as.subscribe_stmt.tag_name, tag_name, sizeof(stmt->as.subscribe_stmt.tag_name));

    if (match(parser, TOKEN_ARROW)) {
        Endpoint ep2;
        if (!parse_endpoint(parser, &ep2)) {
            ast_stmt_free(stmt);
            return NULL;
        }
        stmt->as.subscribe_stmt.from_ep = ep1;
        stmt->as.subscribe_stmt.to_ep = ep2;
    } else {
        stmt->as.subscribe_stmt.from_ep.is_empty = true;
        stmt->as.subscribe_stmt.to_ep = ep1;
    }

    return stmt;
}

// Parse broadcast(pub_tag, subscriber_tags) [:: "payload"]
static AstStmt *parse_broadcast(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'broadcast'

    if (!expect(parser, TOKEN_LPAREN, "Opening '(' after 'broadcast'")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected publisher tag identifier, got '%s'", parser->current.text);
        return NULL;
    }
    char pub_tag[MAX_TAG_NAME_LEN];
    safe_copy_str(pub_tag, parser->current.text, sizeof(pub_tag));
    parser_advance(parser);

    if (!expect(parser, TOKEN_COMMA, "Comma ',' between publisher tag and target registry")) return NULL;

    RegistryType target_reg;
    if (!parse_registry_type(parser, &target_reg)) return NULL;

    if (!expect(parser, TOKEN_RPAREN, "Closing ')' after broadcast arguments")) return NULL;

    char *msg = parse_optional_payload(parser);

    AstStmt *stmt = ast_stmt_create(STMT_BROADCAST, line);
    safe_copy_str(stmt->as.broadcast_stmt.pub_tag, pub_tag, sizeof(stmt->as.broadcast_stmt.pub_tag));
    stmt->as.broadcast_stmt.target_reg = target_reg;
    stmt->as.broadcast_stmt.message = msg;
    return stmt;
}

// Parse multicast(pub_tag, [tag1, tag2, ...]) [:: "payload"]
static AstStmt *parse_multicast(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'multicast'

    if (!expect(parser, TOKEN_LPAREN, "Opening '(' after 'multicast'")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected publisher tag identifier, got '%s'", parser->current.text);
        return NULL;
    }
    char pub_tag[MAX_TAG_NAME_LEN];
    safe_copy_str(pub_tag, parser->current.text, sizeof(pub_tag));
    parser_advance(parser);

    if (!expect(parser, TOKEN_COMMA, "Comma ',' between publisher tag and tag list")) return NULL;

    if (!expect(parser, TOKEN_LBRACKET, "Opening '[' for multicast tag list")) return NULL;

    size_t cap = 8;
    char **tags = (char **)malloc(sizeof(char *) * cap);
    size_t count = 0;

    while (!check(parser, TOKEN_RBRACKET) && !check(parser, TOKEN_EOF) && !parser->has_error) {
        if (check(parser, TOKEN_IDENTIFIER)) {
            if (count >= cap) {
                cap *= 2;
                tags = (char **)realloc(tags, sizeof(char *) * cap);
            }
            tags[count++] = safe_strdup(parser->current.text);
            parser_advance(parser);
            match(parser, TOKEN_COMMA); // optional comma
        } else {
            set_error(parser, "Expected tag identifier in multicast list, got '%s'", parser->current.text);
            for (size_t i = 0; i < count; i++) free(tags[i]);
            free(tags);
            return NULL;
        }
    }

    if (!expect(parser, TOKEN_RBRACKET, "Closing ']' after multicast tag list")) {
        for (size_t i = 0; i < count; i++) free(tags[i]);
        free(tags);
        return NULL;
    }

    if (!expect(parser, TOKEN_RPAREN, "Closing ')' after multicast arguments")) {
        for (size_t i = 0; i < count; i++) free(tags[i]);
        free(tags);
        return NULL;
    }

    char *msg = parse_optional_payload(parser);

    AstStmt *stmt = ast_stmt_create(STMT_MULTICAST, line);
    safe_copy_str(stmt->as.multicast_stmt.pub_tag, pub_tag, sizeof(stmt->as.multicast_stmt.pub_tag));
    stmt->as.multicast_stmt.sub_tags = tags;
    stmt->as.multicast_stmt.sub_tag_count = count;
    stmt->as.multicast_stmt.message = msg;
    return stmt;
}

// Parse send(pub_tag, sub_tag) [:: "payload"]
static AstStmt *parse_send(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'send'

    if (!expect(parser, TOKEN_LPAREN, "Opening '(' after 'send'")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected publisher tag identifier, got '%s'", parser->current.text);
        return NULL;
    }
    char pub_tag[MAX_TAG_NAME_LEN];
    safe_copy_str(pub_tag, parser->current.text, sizeof(pub_tag));
    parser_advance(parser);

    if (!expect(parser, TOKEN_COMMA, "Comma ',' between publisher tag and subscriber tag")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected subscriber tag identifier, got '%s'", parser->current.text);
        return NULL;
    }
    char sub_tag[MAX_TAG_NAME_LEN];
    safe_copy_str(sub_tag, parser->current.text, sizeof(sub_tag));
    parser_advance(parser);

    if (!expect(parser, TOKEN_RPAREN, "Closing ')' after send arguments")) return NULL;

    char *msg = parse_optional_payload(parser);

    AstStmt *stmt = ast_stmt_create(STMT_SEND, line);
    safe_copy_str(stmt->as.send_stmt.pub_tag, pub_tag, sizeof(stmt->as.send_stmt.pub_tag));
    safe_copy_str(stmt->as.send_stmt.sub_tag, sub_tag, sizeof(stmt->as.send_stmt.sub_tag));
    stmt->as.send_stmt.message = msg;
    return stmt;
}

// Parse sleep(...)
static AstStmt *parse_sleep(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'sleep'

    bool has_paren = match(parser, TOKEN_LPAREN);

    if (!check(parser, TOKEN_NUMBER)) {
        set_error(parser, "Expected duration in ms after 'sleep', got '%s'", parser->current.text);
        return NULL;
    }
    int ms = atoi(parser->current.text);
    parser_advance(parser);

    if (has_paren) {
        expect(parser, TOKEN_RPAREN, "Closing ')' after sleep duration");
    }

    AstStmt *stmt = ast_stmt_create(STMT_SLEEP, line);
    stmt->as.sleep_stmt.ms = ms;
    return stmt;
}

// Parse log("message")
static AstStmt *parse_log(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'log'

    bool has_paren = match(parser, TOKEN_LPAREN);

    if (!check(parser, TOKEN_STRING) && !check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected message after 'log', got '%s'", parser->current.text);
        return NULL;
    }
    char *msg = safe_strdup(parser->current.text);
    parser_advance(parser);

    if (has_paren) {
        expect(parser, TOKEN_RPAREN, "Closing ')' after log message");
    }

    AstStmt *stmt = ast_stmt_create(STMT_LOG, line);
    stmt->as.log_stmt.message = msg;
    return stmt;
}

// Parse subscribers[tag] = [ ... ]
static AstStmt *parse_subscriber_block(Parser *parser) {
    int line = parser->current.line;
    parser_advance(parser); // consume 'subscribers'

    if (!expect(parser, TOKEN_LBRACKET, "Expected '[' after 'subscribers'")) return NULL;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        set_error(parser, "Expected tag identifier inside subscribers[...], got '%s'", parser->current.text);
        return NULL;
    }
    char tag_name[MAX_TAG_NAME_LEN];
    safe_copy_str(tag_name, parser->current.text, sizeof(tag_name));
    parser_advance(parser);

    if (!expect(parser, TOKEN_RBRACKET, "Expected ']' after subscriber tag")) return NULL;

    if (!expect(parser, TOKEN_EQUALS, "Expected '=' after subscribers[tag]")) return NULL;

    if (!expect(parser, TOKEN_LBRACKET, "Expected '[' to open statements list")) return NULL;

    AstStmt *block = ast_stmt_create(STMT_SUBSCRIBER_BLOCK, line);
    safe_copy_str(block->as.subscriber_block.tag_name, tag_name, sizeof(block->as.subscriber_block.tag_name));

    size_t cap = 8;
    block->as.subscriber_block.inner_stmts = (AstStmt **)malloc(sizeof(AstStmt *) * cap);
    block->as.subscriber_block.inner_count = 0;

    while (!check(parser, TOKEN_RBRACKET) && !check(parser, TOKEN_EOF) && !parser->has_error) {
        AstStmt *inner = parse_statement(parser);
        if (!inner) {
            break;
        }

        if (block->as.subscriber_block.inner_count >= cap) {
            cap *= 2;
            block->as.subscriber_block.inner_stmts =
                (AstStmt **)realloc(block->as.subscriber_block.inner_stmts, sizeof(AstStmt *) * cap);
        }
        block->as.subscriber_block.inner_stmts[block->as.subscriber_block.inner_count++] = inner;

        match(parser, TOKEN_COMMA); // optional comma
    }

    expect(parser, TOKEN_RBRACKET, "Expected ']' to close statements list");
    return block;
}

static AstStmt *parse_statement(Parser *parser) {
    if (check(parser, TOKEN_KW_PUBLISHER_TAGS)) {
        parser_advance(parser);
        return parse_tags_block(parser, REGISTRY_PUBLISHER);
    }
    if (check(parser, TOKEN_KW_SUBSCRIBER_TAGS)) {
        parser_advance(parser);
        return parse_tags_block(parser, REGISTRY_SUBSCRIBER);
    }
    if (check(parser, TOKEN_KW_SUBSCRIBERS)) {
        return parse_subscriber_block(parser);
    }
    if (check(parser, TOKEN_KW_CHANGE)) {
        return parse_change(parser);
    }
    if (check(parser, TOKEN_KW_DELETE)) {
        return parse_delete(parser);
    }
    if (check(parser, TOKEN_KW_ADD)) {
        return parse_add(parser);
    }
    if (check(parser, TOKEN_KW_SUBSCRIBE)) {
        return parse_subscribe(parser);
    }
    if (check(parser, TOKEN_KW_BROADCAST)) {
        return parse_broadcast(parser);
    }
    if (check(parser, TOKEN_KW_MULTICAST)) {
        return parse_multicast(parser);
    }
    if (check(parser, TOKEN_KW_SEND)) {
        return parse_send(parser);
    }
    if (check(parser, TOKEN_KW_SLEEP)) {
        return parse_sleep(parser);
    }
    if (check(parser, TOKEN_KW_LOG)) {
        return parse_log(parser);
    }

    set_error(parser, "Unexpected token '%s' at beginning of statement", parser->current.text);
    return NULL;
}

Program *parser_parse_program(Parser *parser) {
    Program *prog = program_create();
    LOG_INFO("PARSER", "Starting DSL parsing...");

    while (!check(parser, TOKEN_EOF) && !parser->has_error) {
        AstStmt *stmt = parse_statement(parser);
        if (stmt) {
            program_append_stmt(prog, stmt);
        } else {
            break;
        }
    }

    if (parser->has_error) {
        LOG_ERROR("PARSER", "Parsing failed with error: %s", parser->error_msg);
        program_free(prog);
        return NULL;
    }

    LOG_INFO("PARSER", "Parsing completed successfully. %zu statements parsed.", prog->count);
    return prog;
}
