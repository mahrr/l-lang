#ifndef raven_lexer_h
#define raven_lexer_h

typedef enum {
    // Keywords
    TOKEN_BREAK, TOKEN_COND, TOKEN_ELSE,
    TOKEN_FALSE, TOKEN_FN,   TOKEN_FOR,
    TOKEN_IF,    TOKEN_IN,   TOKEN_LET,
    TOKEN_MATCH, TOKEN_NIL,  TOKEN_RETURN,
    TOKEN_WHILE, TOKEN_TRUE, TOKEN_TYPE,

    // Operators
    TOKEN_PLUS,    TOKEN_MINUS,    TOKEN_STAR,
    TOKEN_SLASH,   TOKEN_PERCENT,  TOKEN_DOT,
    TOKEN_NOT,     TOKEN_AND,      TOKEN_OR,
    TOKEN_AT,      TOKEN_COLON_COLON,
    TOKEN_LESS,    TOKEN_LESS_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_EQUAL,   TOKEN_EQUAL_EQUAL,
    TOKEN_BANG_EQUAL,

    // Delimiters
    TOKEN_DO, TOKEN_END, TOKEN_PIPE,
    TOKEN_HYPHEN_LESS,  TOKEN_COMMA,
    TOKEN_SEMICOLON,    TOKEN_COLON,
    TOKEN_LEFT_PAREN,   TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,   TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,

    // Literals
    TOKEN_IDENTIFIER, TOKEN_NUMBER,
    TOKEN_STRING,

    TOKEN_ERROR,
    TOKEN_EOF,
} TokenType;

typedef struct {
    TokenType type;
    const char *lexeme;
    int length;
    int line;
} Token;

typedef struct {
    const char *start;
    const char *current;
    int line;
} Lexer;

// Initialize a lexer with a given string source.
void init_lexer(Lexer *lexer, const char *source);

// Consume and return the next token.
Token next_token(Lexer *lexer);

#endif
