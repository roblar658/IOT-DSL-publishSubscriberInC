#include "lexer.h"
#include "logger.h"

const char *token_type_name(DslTokenType type) {
    switch (type) {
        case TOKEN_EOF:                return "EOF";
        case TOKEN_ERROR:              return "ERROR";
        case TOKEN_IDENTIFIER:         return "IDENTIFIER";
        case TOKEN_STRING:             return "STRING";
        case TOKEN_NUMBER:             return "NUMBER";
        case TOKEN_ENDPOINT:           return "ENDPOINT";
        case TOKEN_COLONCOLON:         return "::";
        case TOKEN_ARROW:              return "->";
        case TOKEN_LBRACE:             return "{";
        case TOKEN_RBRACE:             return "}";
        case TOKEN_LBRACKET:           return "[";
        case TOKEN_RBRACKET:           return "]";
        case TOKEN_LPAREN:             return "(";
        case TOKEN_RPAREN:             return ")";
        case TOKEN_COMMA:              return ",";
        case TOKEN_COLON:              return ":";
        case TOKEN_EQUALS:             return "=";
        case TOKEN_KW_PUBLISHER_TAGS:  return "publisher_tags";
        case TOKEN_KW_SUBSCRIBER_TAGS: return "subscriber_tags";
        case TOKEN_KW_SUBSCRIBERS:     return "subscribers";
        case TOKEN_KW_CHANGE:          return "change";
        case TOKEN_KW_DELETE:          return "delete";
        case TOKEN_KW_ADD:             return "add";
        case TOKEN_KW_SUBSCRIBE:       return "subscribe";
        case TOKEN_KW_BROADCAST:       return "broadcast";
        case TOKEN_KW_MULTICAST:       return "multicast";
        case TOKEN_KW_SEND:            return "send";
        case TOKEN_KW_EMPTY:           return "EMPTY";
        case TOKEN_KW_SLEEP:           return "sleep";
        case TOKEN_KW_LOG:             return "log";
        default:                       return "UNKNOWN_TOKEN";
    }
}

void lexer_init(Lexer *lexer, const char *source) {
    lexer->source = source ? source : "";
    lexer->length = strlen(lexer->source);
    lexer->cursor = 0;
    lexer->line = 1;
    lexer->col = 1;
    lexer->has_peeked = false;
    memset(&lexer->peeked_token, 0, sizeof(Token));
}

static char peek_char(const Lexer *lexer, size_t offset) {
    if (lexer->cursor + offset >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->cursor + offset];
}

static char advance_char(Lexer *lexer) {
    if (lexer->cursor >= lexer->length) {
        return '\0';
    }
    char c = lexer->source[lexer->cursor++];
    if (c == '\n') {
        lexer->line++;
        lexer->col = 1;
    } else {
        lexer->col++;
    }
    return c;
}

static void skip_whitespace_and_comments(Lexer *lexer) {
    while (lexer->cursor < lexer->length) {
        char c = peek_char(lexer, 0);

        if (isspace((unsigned char)c)) {
            advance_char(lexer);
            continue;
        }

        // Single line comments starting with '#' or '//'
        if (c == '#') {
            while (lexer->cursor < lexer->length && peek_char(lexer, 0) != '\n') {
                advance_char(lexer);
            }
            continue;
        }

        if (c == '/' && peek_char(lexer, 1) == '/') {
            advance_char(lexer);
            advance_char(lexer);
            while (lexer->cursor < lexer->length && peek_char(lexer, 0) != '\n') {
                advance_char(lexer);
            }
            continue;
        }

        // Multi-line comment /* ... */
        if (c == '/' && peek_char(lexer, 1) == '*') {
            advance_char(lexer);
            advance_char(lexer);
            while (lexer->cursor < lexer->length) {
                if (peek_char(lexer, 0) == '*' && peek_char(lexer, 1) == '/') {
                    advance_char(lexer);
                    advance_char(lexer);
                    break;
                }
                advance_char(lexer);
            }
            continue;
        }

        break;
    }
}

static bool is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-';
}

static Token scan_string(Lexer *lexer, char quote) {
    Token token;
    token.type = TOKEN_STRING;
    token.line = lexer->line;
    token.col = lexer->col;
    token.text[0] = '\0';

    advance_char(lexer); // consume opening quote

    size_t len = 0;
    while (lexer->cursor < lexer->length) {
        char c = advance_char(lexer);
        if (c == quote) {
            token.text[len] = '\0';
            return token;
        }
        if (c == '\\') {
            char next = advance_char(lexer);
            if (next == 'n') c = '\n';
            else if (next == 't') c = '\t';
            else if (next == 'r') c = '\r';
            else if (next == '\\') c = '\\';
            else if (next == quote) c = quote;
        }
        if (len < sizeof(token.text) - 1) {
            token.text[len++] = c;
        }
    }

    token.type = TOKEN_ERROR;
    snprintf(token.text, sizeof(token.text), "Unterminated string literal at line %d", token.line);
    return token;
}

static Token scan_ident_or_endpoint(Lexer *lexer) {
    Token token;
    token.line = lexer->line;
    token.col = lexer->col;

    size_t start = lexer->cursor;

    while (lexer->cursor < lexer->length && is_ident_char(peek_char(lexer, 0))) {
        advance_char(lexer);
    }

    bool has_port_colon = false;
    // Check if followed by single ':' and port (e.g. 127.0.0.1:8001 or host:port)
    // NEVER consume if next is '::'
    if (peek_char(lexer, 0) == ':' && peek_char(lexer, 1) != ':') {
        if (isalnum((unsigned char)peek_char(lexer, 1))) {
            advance_char(lexer); // consume ':'
            has_port_colon = true;
            while (lexer->cursor < lexer->length && is_ident_char(peek_char(lexer, 0))) {
                advance_char(lexer);
            }
        }
    }

    size_t len = lexer->cursor - start;
    if (len >= sizeof(token.text)) {
        len = sizeof(token.text) - 1;
    }
    strncpy(token.text, lexer->source + start, len);
    token.text[len] = '\0';

    if (has_port_colon) {
        token.type = TOKEN_ENDPOINT;
        return token;
    }

    // Keyword lookup
    if (strcmp(token.text, "publisher_tags") == 0)   token.type = TOKEN_KW_PUBLISHER_TAGS;
    else if (strcmp(token.text, "subscriber_tags") == 0)  token.type = TOKEN_KW_SUBSCRIBER_TAGS;
    else if (strcmp(token.text, "subscribers") == 0)      token.type = TOKEN_KW_SUBSCRIBERS;
    else if (strcmp(token.text, "change") == 0)           token.type = TOKEN_KW_CHANGE;
    else if (strcmp(token.text, "delete") == 0)           token.type = TOKEN_KW_DELETE;
    else if (strcmp(token.text, "add") == 0)              token.type = TOKEN_KW_ADD;
    else if (strcmp(token.text, "subscribe") == 0)        token.type = TOKEN_KW_SUBSCRIBE;
    else if (strcmp(token.text, "broadcast") == 0)        token.type = TOKEN_KW_BROADCAST;
    else if (strcmp(token.text, "multicast") == 0)        token.type = TOKEN_KW_MULTICAST;
    else if (strcmp(token.text, "send") == 0)             token.type = TOKEN_KW_SEND;
    else if (strcmp(token.text, "EMPTY") == 0)            token.type = TOKEN_KW_EMPTY;
    else if (strcmp(token.text, "sleep") == 0)            token.type = TOKEN_KW_SLEEP;
    else if (strcmp(token.text, "log") == 0)              token.type = TOKEN_KW_LOG;
    else {
        // Check if all digits
        bool all_digits = true;
        for (size_t i = 0; i < len; i++) {
            if (!isdigit((unsigned char)token.text[i])) {
                all_digits = false;
                break;
            }
        }
        token.type = all_digits ? TOKEN_NUMBER : TOKEN_IDENTIFIER;
    }

    return token;
}

Token lexer_next_token(Lexer *lexer) {
    if (lexer->has_peeked) {
        lexer->has_peeked = false;
        return lexer->peeked_token;
    }

    skip_whitespace_and_comments(lexer);

    Token token;
    token.line = lexer->line;
    token.col = lexer->col;
    token.text[0] = '\0';

    if (lexer->cursor >= lexer->length) {
        token.type = TOKEN_EOF;
        strcpy(token.text, "EOF");
        return token;
    }

    char c = peek_char(lexer, 0);

    // Strings
    if (c == '"' || c == '\'') {
        return scan_string(lexer, c);
    }

    // Double colon ::
    if (c == ':' && peek_char(lexer, 1) == ':') {
        advance_char(lexer);
        advance_char(lexer);
        token.type = TOKEN_COLONCOLON;
        strcpy(token.text, "::");
        return token;
    }

    // Arrow ->
    if (c == '-' && peek_char(lexer, 1) == '>') {
        advance_char(lexer);
        advance_char(lexer);
        token.type = TOKEN_ARROW;
        strcpy(token.text, "->");
        return token;
    }

    // Single character tokens
    switch (c) {
        case '{':
            advance_char(lexer);
            token.type = TOKEN_LBRACE;
            strcpy(token.text, "{");
            return token;
        case '}':
            advance_char(lexer);
            token.type = TOKEN_RBRACE;
            strcpy(token.text, "}");
            return token;
        case '[':
            advance_char(lexer);
            token.type = TOKEN_LBRACKET;
            strcpy(token.text, "[");
            return token;
        case ']':
            advance_char(lexer);
            token.type = TOKEN_RBRACKET;
            strcpy(token.text, "]");
            return token;
        case '(':
            advance_char(lexer);
            token.type = TOKEN_LPAREN;
            strcpy(token.text, "(");
            return token;
        case ')':
            advance_char(lexer);
            token.type = TOKEN_RPAREN;
            strcpy(token.text, ")");
            return token;
        case ',':
            advance_char(lexer);
            token.type = TOKEN_COMMA;
            strcpy(token.text, ",");
            return token;
        case ':':
            advance_char(lexer);
            token.type = TOKEN_COLON;
            strcpy(token.text, ":");
            return token;
        case '=':
            advance_char(lexer);
            token.type = TOKEN_EQUALS;
            strcpy(token.text, "=");
            return token;
    }

    // Identifiers, numbers, endpoints
    if (is_ident_char(c)) {
        return scan_ident_or_endpoint(lexer);
    }

    // Unknown character error
    advance_char(lexer);
    token.type = TOKEN_ERROR;
    snprintf(token.text, sizeof(token.text), "Unexpected character '%c' (0x%02X) at line %d:%d",
             c, (unsigned char)c, token.line, token.col);
    return token;
}

Token lexer_peek_token(Lexer *lexer) {
    if (!lexer->has_peeked) {
        lexer->peeked_token = lexer_next_token(lexer);
        lexer->has_peeked = true;
    }
    return lexer->peeked_token;
}
