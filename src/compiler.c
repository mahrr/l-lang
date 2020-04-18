#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "lexer.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#if defined(DEBUG_DUMP_CODE) || defined(DEBUG_TRACE_PARSING)
#include <stdarg.h>
#include "debug.h"
#endif

/*
 * A Pratt parser is used for expression parsing. A parsing rule
 * is defined with a ParseRule struct. The parser uses a parsing
 * rule table to know the precedence, prefix parsing function and
 * parsing infix function for each token type.
*/

// Parser state
typedef struct {
    Lexer *lexer;     // The input, token stream
    VM *vm;           // The output chunk, bytecode stream
    
    Token current;    // Current consumed token
    Token previous;   // Previously consumed token
    
    bool had_error;   // Error flag to stop bytecode execution later
    bool panic_mode;  // If set, any parsing error will be ignored

#ifdef DEBUG_TRACE_PARSING
    int level;        // Parser nesting level, for debugging
#endif
} Parser;

// Expressions precedence, from low to high
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_CONS,        // ::
    PREC_CONCAT,      // @
    PREC_TERM,        // + -
    PREC_FACTOR,      // * / %
    PREC_UNARY,       // not -
    PREC_CALL,        // ()
    PREC_HIGHEST,     // Group [] .
} Precedence;

typedef void (*ParseFn)(Parser *);

// Parser rule for a token type
typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

/** Parser Tracing **/

#ifdef DEBUG_TRACE_PARSING

static char *strings_precedences[] = {
    "None",
    "Assignment",
    "Or",
    "And",
    "Equality",
    "Comparison",
    "Cons",
    "Concat",
    "Term",
    "Factor",
    "Unary",
    "Call",
    "Highest",
};

static void debug_log(Parser *parser, const char *fmt, ...) {
    va_list arguments;
    va_start(arguments, fmt);

    for (int i = 0; i < parser->level; i++) printf("| ");
    vprintf(fmt, arguments);
    putchar('\n');

    va_end(arguments);
    parser->level += 1;
}

#endif

/** Error Reporting **/

static void error(Parser *parser, Token *where, const char *message) {
    if (parser->panic_mode) return;
    parser->panic_mode = true;

    fprintf(stderr, "[line %d] SyntaxError", where->line);

    if (where->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (where->type != TOKEN_ERROR) {
        fprintf(stderr, " at '%.*s'", where->length, where->lexeme);
    }

    fprintf(stderr, ": %s\n", message);
    parser->had_error = true;
}

static inline void error_previous(Parser *parser, const char *message) {
    error(parser, &parser->previous, message);
}

static inline void error_current(Parser *parser, const char *message) {
    error(parser, &parser->current, message);
}

/** Parser State **/

static void advance(Parser *parser) {
    parser->previous = parser->current;
    
    for (;;) {
        parser->current = next_token(parser->lexer);

        if (parser->current.type != TOKEN_ERROR) break;
    }
}

static void consume(Parser *parser, TokenType type, const char *msg) {
    if (parser->current.type == type) {
        advance(parser);
    } else {
        error_current(parser, msg);
    }
}

/** Emitting **/

static inline void emit_byte(Parser *parser, uint8_t byte) {
    write_byte(parser->vm->chunk, byte, parser->previous.line);
}

static inline void emit_bytes(Parser *parser, uint8_t x, uint8_t y) {
    emit_byte(parser, x);
    emit_byte(parser, y);
}

static inline int emit_jump(Parser *parser, uint8_t instruction) {
    emit_byte(parser, instruction);
    emit_bytes(parser, 0xff, 0xff);
    return parser->vm->chunk->count - 2;
}

static inline void patch_jump(Parser *parser, int from) {
    // -2 because of the jmp instruction 2-bytes immediate argument
    int offset = parser->vm->chunk->count - from - 2;

    if (offset > UINT16_MAX) {
        error_current(parser, "Jump offset exceeds the allowed limit");
    }

    parser->vm->chunk->opcodes[from] = (offset >> 8) & 0xff;
    parser->vm->chunk->opcodes[from + 1] = offset & 0xff;
}

static uint8_t make_constant(Parser *parser, Value value) {
    int constant_index = write_constant(parser->vm->chunk, value);

    if (constant_index > UINT8_MAX) {
        error_current(parser, "Too many constants in one chunk");
        return 0;
    }

    return (uint8_t)constant_index;
}

static inline void emit_constant(Parser *parser, Value value) {
    emit_bytes(parser, OP_LOAD_CONST, make_constant(parser, value));
}

/** Parsing **/

static void expression(Parser *);
static void parse(Parser *, Precedence);
static ParseRule *token_rule(TokenType);

static void binary(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "binary");
#endif
    
    TokenType operator = parser->previous.type;

    ParseRule *rule = token_rule(operator);
    parse(parser, (Precedence)(rule->precedence + 1));

    switch (operator) {
    case TOKEN_PLUS:          emit_byte(parser, OP_ADD); break;
    case TOKEN_MINUS:         emit_byte(parser, OP_SUB); break;
    case TOKEN_STAR:          emit_byte(parser, OP_MUL); break;
    case TOKEN_SLASH:         emit_byte(parser, OP_DIV); break;
    case TOKEN_PERCENT:       emit_byte(parser, OP_MOD); break;
    case TOKEN_LESS:          emit_byte(parser, OP_LT);  break;
    case TOKEN_LESS_EQUAL:    emit_byte(parser, OP_LTQ); break;
    case TOKEN_GREATER:       emit_byte(parser, OP_GT);  break;
    case TOKEN_GREATER_EQUAL: emit_byte(parser, OP_GTQ); break;
    case TOKEN_EQUAL_EQUAL:   emit_byte(parser, OP_EQ);  break;
    case TOKEN_BANG_EQUAL:    emit_byte(parser, OP_NEQ); break;
    default:
        assert(0);
    }

#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif
}

static void and_(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "and");
#endif

    int jump = emit_jump(parser, OP_JMP_FALSE);

    emit_byte(parser, OP_POP);
    parse(parser, PREC_AND + 1);

    patch_jump(parser, jump);

#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif
}

static void or_(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "or");
#endif

    // first operand is falsy
    int false_jump = emit_jump(parser, OP_JMP_FALSE);

    // first operand is not falsy
    int true_jump = emit_jump(parser, OP_JMP);

    patch_jump(parser, false_jump);

    emit_byte(parser, OP_POP);
    parse(parser, PREC_OR + 1);

    patch_jump(parser, true_jump);

#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif
}

static void grouping(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "grouping");
#endif
    
    expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN,
            "Expect closing ')' after group expression");

#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif
}

static void number(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "number");
#endif
    
    Value value = Num_Value(strtod(parser->previous.lexeme, NULL));
    emit_constant(parser, value);
    
#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif
}

static void string(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "string");
#endif

    // +1 and -2 for the literal string quotes
    ObjString *string = copy_string(parser->vm,
                                    parser->previous.lexeme + 1,
                                    parser->previous.length - 2);
    emit_constant(parser, Obj_Value(string));
    
#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif

}

static void boolean(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "boolean");
#endif
    
    TokenType type = parser->previous.type;
    emit_byte(parser, type == TOKEN_TRUE ? OP_LOAD_TRUE : OP_LOAD_FALSE);

#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif
}

static void nil(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "nil");
#endif
    
    emit_byte(parser, OP_LOAD_NIL);

#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif
}

static void unary(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "unary");
#endif
    
    TokenType operator = parser->previous.type;
    parse(parser, PREC_UNARY);

    switch (operator) {
    case TOKEN_MINUS: emit_byte(parser, OP_NEG); break;
    case TOKEN_NOT:   emit_byte(parser, OP_NOT); break;
    default:
        assert(0);
    }

#ifdef DEBUG_TRACE_PARSING
    parser->level -= 1;
#endif
}

// Parsing rule table
static ParseRule rules[] = {
    { NULL,     NULL,   PREC_NONE },         // TOKEN_BREAK
    { NULL,     NULL,   PREC_NONE },         // TOKEN_COND
    { NULL,     NULL,   PREC_NONE },         // TOKEN_ELSE
    { boolean,  NULL,   PREC_NONE },         // TOKEN_FALSE
    { NULL,     NULL,   PREC_NONE },         // TOKEN_FN
    { NULL,     NULL,   PREC_NONE },         // TOKEN_FOR
    { NULL,     NULL,   PREC_NONE },         // TOKEN_IF
    { NULL,     NULL,   PREC_NONE },         // TOKEN_IN
    { NULL,     NULL,   PREC_NONE },         // TOKEN_LET
    { NULL,     NULL,   PREC_NONE },         // TOKEN_MATCH
    { nil,      NULL,   PREC_NONE },         // TOKEN_NIL
    { NULL,     NULL,   PREC_NONE },         // TOKEN_RETURN
    { NULL,     NULL,   PREC_NONE },         // TOKEN_WHILE
    { boolean,  NULL,   PREC_NONE },         // TOKEN_TRUE
    { NULL,     NULL,   PREC_NONE },         // TOKEN_TYPE
    { NULL,     binary, PREC_TERM },         // TOKEN_PLUS
    { unary,    binary, PREC_TERM },         // TOKEN_MINUS
    { NULL,     binary, PREC_FACTOR },       // TOKEN_STAR
    { NULL,     binary, PREC_FACTOR },       // TOKEN_SLASH
    { NULL,     binary, PREC_FACTOR },       // TOKEN_PERCENT
    { NULL,     NULL,   PREC_NONE },         // TOKEN_DOT
    { unary,    NULL,   PREC_NONE },         // TOKEN_NOT
    { NULL,     and_,   PREC_AND },          // TOKEN_AND
    { NULL,     or_,    PREC_OR  },          // TOKEN_OR
    { NULL,     NULL,   PREC_NONE },         // TOKEN_AT
    { NULL,     NULL,   PREC_NONE },         // TOKEN_COLON_COLON
    { NULL,     binary, PREC_COMPARISON },   // TOKEN_LT
    { NULL,     binary, PREC_COMPARISON },   // TOKEN_LT_EQUAL
    { NULL,     binary, PREC_COMPARISON },   // TOKEN_GT
    { NULL,     binary, PREC_COMPARISON },   // TOKEN_GT_EQUAL
    { NULL,     NULL,   PREC_NONE },         // TOKEN_EQUAL
    { NULL,     binary, PREC_EQUALITY },     // TOKEN_EQUAL_EQUAL
    { NULL,     binary, PREC_EQUALITY },     // TOKEN_BANG_EQUAL
    { NULL,     NULL,   PREC_NONE },         // TOKEN_DO
    { NULL,     NULL,   PREC_NONE },         // TOKEN_END
    { NULL,     NULL,   PREC_NONE },         // TOKEN_PIPE
    { NULL,     NULL,   PREC_NONE },         // TOKEN_HYPHEN_LT
    { NULL,     NULL,   PREC_NONE },         // TOKEN_COMMA
    { NULL,     NULL,   PREC_NONE },         // TOKEN_SEMICOLON
    { NULL,     NULL,   PREC_NONE },         // TOKEN_COLON
    { grouping, NULL,   PREC_NONE },         // TOKEN_LEFT_PAREN
    { NULL,     NULL,   PREC_NONE },         // TOKEN_RIGHT_PAREN
    { NULL,     NULL,   PREC_NONE },         // TOKEN_LEFT_BRACE
    { NULL,     NULL,   PREC_NONE },         // TOKEN_RIGHT_BRACE
    { NULL,     NULL,   PREC_NONE },         // TOKEN_LEFT_BRACKET
    { NULL,     NULL,   PREC_NONE },         // TOKEN_RIGHT_BRACKET
    { NULL,     NULL,   PREC_NONE },         // TOKEN_IDENTIFIER
    { number,   NULL,   PREC_NONE },         // TOKEN_NUMBER
    { string,   NULL,   PREC_NONE },         // TOKEN_STRING
    { NULL,     NULL,   PREC_NONE },         // TOKEN_ERROR
    { NULL,     NULL,   PREC_NONE },         // TOKEN_EOF
};

// Return the parsing rule of a given token type.
static inline ParseRule *token_rule(TokenType type) {
    return &rules[type];
}

static void parse(Parser *parser, Precedence precedence) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "parse(%s)", strings_precedences[precedence]);
#endif
    
    advance(parser);

    ParseFn prefix = token_rule(parser->previous.type)->prefix;
    if (prefix == NULL) {
        error_previous(parser, "Unexpected token, expect expression");
        return;
    }

    prefix(parser);
    while (precedence <= token_rule(parser->current.type)->precedence) {
        advance(parser);
        ParseFn infix = token_rule(parser->previous.type)->infix;
        infix(parser);
    }

#ifdef DEBUG_TRACE_EXECUTION
    parser->level -=1;
#endif
}

static inline void expression(Parser *parser) {
#ifdef DEBUG_TRACE_PARSING
    debug_log(parser, "expression");
#endif
    
    parse(parser, PREC_ASSIGNMENT);

#ifdef DEBUG_TRACE_EXECUTION
    parser->level -= 1;
#endif
}

bool compile(VM *vm, const char *source) {
    Lexer lexer;
    init_lexer(&lexer, source);
    
    Parser parser;
    parser.lexer = &lexer;
    parser.vm = vm;
    parser.had_error = false;
    parser.panic_mode = false;
    
#ifdef DEBUG_TRACE_PARSING
    parser.level = 0;
#endif

    advance(&parser);
    expression(&parser);
    consume(&parser, TOKEN_EOF, "Expect end of expression");

    emit_byte(&parser, OP_RETURN);

#ifdef DEBUG_DUMP_CODE
    disassemble_chunk(vm->chunk, "top-level");
#endif
    
    return !parser.had_error;
}
