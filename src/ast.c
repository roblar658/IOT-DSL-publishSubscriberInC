#include "ast.h"

const char *registry_type_name(RegistryType reg) {
    switch (reg) {
        case REGISTRY_PUBLISHER:  return "publisher_tags";
        case REGISTRY_SUBSCRIBER: return "subscriber_tags";
        default:                  return "unknown_registry";
    }
}

Program *program_create(void) {
    Program *prog = (Program *)malloc(sizeof(Program));
    if (!prog) return NULL;
    prog->count = 0;
    prog->capacity = 16;
    prog->statements = (AstStmt **)malloc(sizeof(AstStmt *) * prog->capacity);
    return prog;
}

void program_append_stmt(Program *prog, AstStmt *stmt) {
    if (!prog || !stmt) return;
    if (prog->count >= prog->capacity) {
        prog->capacity *= 2;
        prog->statements = (AstStmt **)realloc(prog->statements, sizeof(AstStmt *) * prog->capacity);
    }
    prog->statements[prog->count++] = stmt;
}

void program_free(Program *prog) {
    if (!prog) return;
    for (size_t i = 0; i < prog->count; i++) {
        ast_stmt_free(prog->statements[i]);
    }
    free(prog->statements);
    free(prog);
}

AstStmt *ast_stmt_create(AstStmtType type, int line) {
    AstStmt *stmt = (AstStmt *)malloc(sizeof(AstStmt));
    if (!stmt) return NULL;
    memset(stmt, 0, sizeof(AstStmt));
    stmt->type = type;
    stmt->line = line;
    return stmt;
}

void ast_stmt_free(AstStmt *stmt) {
    if (!stmt) return;
    switch (stmt->type) {
        case STMT_TAGS_BLOCK:
            if (stmt->as.tags_block.items) {
                free(stmt->as.tags_block.items);
            }
            break;

        case STMT_BROADCAST:
            if (stmt->as.broadcast_stmt.message) {
                free(stmt->as.broadcast_stmt.message);
            }
            break;

        case STMT_MULTICAST:
            if (stmt->as.multicast_stmt.sub_tags) {
                for (size_t i = 0; i < stmt->as.multicast_stmt.sub_tag_count; i++) {
                    free(stmt->as.multicast_stmt.sub_tags[i]);
                }
                free(stmt->as.multicast_stmt.sub_tags);
            }
            if (stmt->as.multicast_stmt.message) {
                free(stmt->as.multicast_stmt.message);
            }
            break;

        case STMT_SEND:
            if (stmt->as.send_stmt.message) {
                free(stmt->as.send_stmt.message);
            }
            break;

        case STMT_SUBSCRIBER_BLOCK:
            if (stmt->as.subscriber_block.inner_stmts) {
                for (size_t i = 0; i < stmt->as.subscriber_block.inner_count; i++) {
                    ast_stmt_free(stmt->as.subscriber_block.inner_stmts[i]);
                }
                free(stmt->as.subscriber_block.inner_stmts);
            }
            break;

        case STMT_LOG:
            if (stmt->as.log_stmt.message) {
                free(stmt->as.log_stmt.message);
            }
            break;

        default:
            break;
    }
    free(stmt);
}

const char *ast_stmt_type_name(AstStmtType type) {
    switch (type) {
        case STMT_TAGS_BLOCK:       return "TAGS_BLOCK";
        case STMT_CHANGE:           return "CHANGE";
        case STMT_DELETE:           return "DELETE";
        case STMT_ADD:              return "ADD";
        case STMT_SUBSCRIBE:        return "SUBSCRIBE";
        case STMT_BROADCAST:        return "BROADCAST";
        case STMT_MULTICAST:        return "MULTICAST";
        case STMT_SEND:             return "SEND";
        case STMT_SUBSCRIBER_BLOCK: return "SUBSCRIBER_BLOCK";
        case STMT_SLEEP:            return "SLEEP";
        case STMT_LOG:              return "LOG";
        default:                    return "UNKNOWN";
    }
}
