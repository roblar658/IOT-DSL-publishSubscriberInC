#ifndef TOKEN_H
#define TOKEN_H

#include "common.h"

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_ERROR,

    // Literals & Identifiers
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_ENDPOINT,

    // Symbols & Punctuation
    TOKEN_COLONCOLON,       // ::
    TOKEN_ARROW,             // ->
    TOKEN_LBRACE,            // {
    TOKEN_RBRACE,            // }
    TOKEN_LBRACKET,          // [
    TOKEN_RBRACKET,          // ]
    TOKEN_LPAREN,            // (
    TOKEN_RPAREN,            // )
    TOKEN_COMMA,             // ,
    TOKEN_COLON,             // :
    TOKEN_EQUALS,            // =

    // Keywords
    TOKEN_KW_PUBLISHER_TAGS, // publisher_tags
    TOKEN_KW_SUBSCRIBER_TAGS,// subscriber_tags
    TOKEN_KW_SUBSCRIBERS,    // subscribers
    TOKEN_KW_CHANGE,         // change
    TOKEN_KW_DELETE,         // delete
    TOKEN_KW_ADD,            // add
    TOKEN_KW_SUBSCRIBE,      // subscribe
    TOKEN_KW_BROADCAST,      // broadcast
    TOKEN_KW_MULTICAST,      // multicast
    TOKEN_KW_SEND,           // send
    TOKEN_KW_EMPTY,          // EMPTY
    TOKEN_KW_SLEEP,          // sleep
    TOKEN_KW_LOG             // log
} DslTokenType;

typedef struct {
    DslTokenType type;
    char text[256];
    int line;
    int col;
} Token;

const char *token_type_name(DslTokenType type);

#endif // TOKEN_H
