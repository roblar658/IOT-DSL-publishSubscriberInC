#ifndef LEXER_H
#define LEXER_H

#include "token.h"

typedef struct {
    const char *source;
    size_t length;
    size_t cursor;
    int line;
    int col;
    Token peeked_token;
    bool has_peeked;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token lexer_next_token(Lexer *lexer);
Token lexer_peek_token(Lexer *lexer);

#endif // LEXER_H
