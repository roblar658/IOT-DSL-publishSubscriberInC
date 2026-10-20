#ifndef AST_H
#define AST_H

#include "common.h"
#include "net.h"

typedef enum {
    REGISTRY_PUBLISHER = 0,
    REGISTRY_SUBSCRIBER
} RegistryType;

const char *registry_type_name(RegistryType reg);

typedef enum {
    STMT_TAGS_BLOCK = 1,
    STMT_CHANGE,
    STMT_DELETE,
    STMT_ADD,
    STMT_SUBSCRIBE,
    STMT_BROADCAST,
    STMT_MULTICAST,
    STMT_SEND,
    STMT_SUBSCRIBER_BLOCK,
    STMT_SLEEP,
    STMT_LOG
} AstStmtType;

typedef struct {
    char tag_name[MAX_TAG_NAME_LEN];
    Endpoint endpoint;
} AstTagItem;

typedef struct AstStmt {
    AstStmtType type;
    int line;

    union {
        // STMT_TAGS_BLOCK
        struct {
            RegistryType reg_type;
            AstTagItem *items;
            size_t item_count;
        } tags_block;

        // STMT_CHANGE
        struct {
            RegistryType reg_type;
            char tag_name[MAX_TAG_NAME_LEN];
            bool is_empty;
            Endpoint from_ep;
            Endpoint to_ep;
        } change_stmt;

        // STMT_DELETE
        struct {
            RegistryType reg_type;
            char tag_name[MAX_TAG_NAME_LEN];
        } delete_stmt;

        // STMT_ADD
        struct {
            RegistryType reg_type;
            char tag_name[MAX_TAG_NAME_LEN];
            bool is_empty;
            Endpoint from_ep;
            Endpoint to_ep;
        } add_stmt;

        // STMT_SUBSCRIBE
        struct {
            RegistryType reg_type;
            char tag_name[MAX_TAG_NAME_LEN];
            Endpoint from_ep;
            Endpoint to_ep;
        } subscribe_stmt;

        // STMT_BROADCAST
        struct {
            char pub_tag[MAX_TAG_NAME_LEN];
            RegistryType target_reg;
            char *message;
        } broadcast_stmt;

        // STMT_MULTICAST
        struct {
            char pub_tag[MAX_TAG_NAME_LEN];
            char **sub_tags;
            size_t sub_tag_count;
            char *message;
        } multicast_stmt;

        // STMT_SEND
        struct {
            char pub_tag[MAX_TAG_NAME_LEN];
            char sub_tag[MAX_TAG_NAME_LEN];
            char *message;
        } send_stmt;

        // STMT_SUBSCRIBER_BLOCK: subscribers[tag] = [ ... ]
        struct {
            char tag_name[MAX_TAG_NAME_LEN];
            struct AstStmt **inner_stmts;
            size_t inner_count;
        } subscriber_block;

        // STMT_SLEEP
        struct {
            int ms;
        } sleep_stmt;

        // STMT_LOG
        struct {
            char *message;
        } log_stmt;
    } as;
} AstStmt;

typedef struct {
    AstStmt **statements;
    size_t count;
    size_t capacity;
} Program;

Program *program_create(void);
void program_append_stmt(Program *prog, AstStmt *stmt);
void program_free(Program *prog);

AstStmt *ast_stmt_create(AstStmtType type, int line);
void ast_stmt_free(AstStmt *stmt);

#endif // AST_H
