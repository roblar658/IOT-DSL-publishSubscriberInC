#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer lexer;
    Token current;
    bool has_error;
    char error_msg[256];
} Parser;

void parser_init(Parser *parser, const char *source);
Program *parser_parse_program(Parser *parser);

#endif // PARSER_H
