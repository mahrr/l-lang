/*
 * (parser.c | 27 Feb 19 | Ahmad Maher, Kareem hamdy)
 *
 * Raven Parser
 *
*/

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "array.h"
#include "ast.h"
#include "debug.h"
#include "parser.h"
#include "strutil.h"
#include "token.h"


/*** DATA ***/

typedef AST_expr (*Prefix_F)(Parser*);
typedef AST_expr (*Infix_F)(Parser*, AST_expr);

/* precedences */
typedef enum {
    LOW_PREC,
    ASSIGN_PREC,
    OR_PREC,
    AND_PREC,
    EQ_PREC,
    ORD_PREC,
    LCONS_PREC,
    CONS_PREC,
    CONC_PREC,
    SUM_PREC,
    MUL_PREC,
    UNARY_PREC,
    HIGH_PREC
} Prec;

/* a message buffer. no parse error message will
   be more than 1028 character long. */
static char error_msg[1028];


/*** HELPERS ***/

/* peek at the current token */
#define curr_token(p) (p)->curr

/* check if the current token has type 't' */
#define curr_token_is(p, t) ((p)->curr->type == (t))

/* check if the previous token has type 't' */
#define prev_token_is(p, t) ((p)->prev->type == (t))

/* check if the next token has type 't' */
#define peek_token_is(p, t) ((p)->peek->type == (t))

/* check if parser reaches the end of the token list */
#define at_end(p) curr_token_is(p, TK_EOF)

/* skip newline tokens */
#define skip_newlines(p)                            \
    while (curr_token_is(p, TK_NL)) next_token(p)

/* fetch the current token and increment the token list*/
static Token *next_token(Parser *p) {
    if (!at_end(p)) {
        p->prev = p->curr;
        p->curr++;
        p->peek++;

        return p->prev;
    }

    return curr_token(p);  /* TK_EOF */
}

/* increment if the peek token has type 't' */
static Token *match_token(Parser *p, TK_type type) {
    if (curr_token_is(p, type))
        return next_token(p);

    return NULL;
}

/* check if the current token not from the given token types 'ap' */
static int curr_token_not(Parser *p, int n, va_list ap) {
    for (int i = 0; i < n; i++) {
        TK_type t = va_arg(ap, TK_type);
        if (curr_token_is(p, t)) return 0;
    }

    return 1;
}

/* register a new error in the parser errors list */
static void reg_error(Parser *p, const char *message) {
    SErr error;
    error.message = strdup(message);

    /* if the current token is a newline token,
       use the previous token as the error place. */
    error.where = curr_token_is(p, TK_NL) ?
        *(p->prev) : *(p->curr);
    ARR_ADD(&p->errors, error);
    p->been_error = 1;
}

/* 
 * return expected token if it has type 't' then increment 
 * the parser token list, otherwise return NULL and register 
 * a new error message.
*/
static Token *expect_token(Parser *p, TK_type t, char *expected) {
    if (curr_token_is(p, t)) {
        return next_token(p);   /* consume current */
    }

    sprintf(error_msg, "%s is expected", expected);
    reg_error(p, error_msg);
    return NULL;
}

/* return the precedence of the token 'tok' */ 
static Prec prec_of(Token *tok) {
    switch (tok->type) {
        
    case TK_LPAREN:
    case TK_LBRACKET:
    case TK_DOT:
        return HIGH_PREC;
        
    case TK_NOT:
        return UNARY_PREC;

    case TK_ASTERISK:
    case TK_SLASH:
    case TK_PERCENT:
        return MUL_PREC;

    case TK_PLUS:
    case TK_MINUS:
        return SUM_PREC;

    case TK_AT:
        return CONC_PREC;

    case TK_PIPE:
        return CONS_PREC;

    case TK_GT:
    case TK_LT:
    case TK_GT_EQ:
    case TK_LT_EQ:
        return ORD_PREC;

    case TK_EQ_EQ:
    case TK_BANG_EQ:
        return EQ_PREC;

    case TK_AND:
        return AND_PREC;

    case TK_OR:
        return OR_PREC;

    case TK_EQ:
        return ASSIGN_PREC;
        
    default:
        return LOW_PREC;
    }
}


/*** INTERNALS ***/

/** patterns nodes **/

static AST_patt pattern(Parser*);
static AST_patt *patterns(Parser*, TK_type, TK_type, char*);

static AST_patt const_patt(Parser *p) {
    Token *tok = next_token(p);
    AST_patt patt = malloc(sizeof (*patt));

    switch (tok->type) {
    case TK_INT:
        patt->obj.i = int_of_tok(tok);
        patt->type = INT_CPATT;
        break;

    case TK_FLOAT:
        patt->obj.f = float_of_tok(tok);
        patt->type = FLOAT_CPATT;
        break;

    case TK_RSTR:
        patt->obj.s = str_of_tok(tok);
        patt->type = RSTR_CPATT;
        break;

    case TK_STR:
        patt->obj.s = str_of_tok(tok);
        patt->type = STR_CPATT;
        break;

    default:
        /* impossible but to discard compiler warnings */
        break;
    }
    
    return patt;
}

static AST_patt hash_patt(Parser *p) {
    next_token(p);  /* consume '{' */

    ARRAY(char*) names;
    ARR_INIT(&names, char*);
    
    ARRAY(AST_patt) patts;
    ARR_INIT(&patts, AST_patt);

    if (!match_token(p, TK_RBRACE)) {
        do {
            Token *ident = expect_token(p, TK_IDENT, "field name");
            if (ident == NULL) return NULL;

            if (!expect_token(p, TK_COLON, "':'"))
                return NULL;

            AST_patt patt = pattern(p);
            if (patt == NULL) return NULL;

            ARR_ADD(&names, ident_of_tok(ident));
            ARR_ADD(&patts, patt);
        } while (match_token(p, TK_COMMA));

        if (!expect_token(p, TK_RBRACE, "}"))
            return NULL;
    }
    ARR_ADD(&names, NULL);
    ARR_ADD(&patts, NULL);
    
    AST_hash_patt hash = malloc(sizeof (*hash));
    hash->names = names.elems;
    hash->patts = patts.elems;
    
    AST_patt patt = malloc(sizeof (*patt));
    patt->type = HASH_PATT;
    patt->obj.hash = hash;

    return patt;
}

static AST_patt ident_patt(Parser *p) {
    Token *ident = next_token(p);
    
    AST_patt patt = malloc(sizeof (*patt));
    patt->type = IDENT_PATT;
    patt->obj.ident = ident_of_tok(ident);

    return patt;
}

static AST_patt list_patt(Parser *p) {
    next_token(p);  /* consume '[' token */

    AST_patt *patts = patterns(p, TK_COMMA, TK_RBRACKET, "]");
    if (patts == NULL) return NULL;

    AST_list_patt list = malloc(sizeof (*list));
    list->patts = patts;

    AST_patt patt = malloc(sizeof (*patt));
    patt->type = LIST_PATT;
    patt->obj.list = list;

    return patt;
}

static AST_patt pair_patt(Parser *p) {
    next_token(p);  /* consume '(' token */

    AST_patt hd = pattern(p);
    if (hd == NULL) return NULL;

    if (!expect_token(p, TK_PIPE, "'|'"))
        return NULL;

    AST_patt tl = pattern(p);
    if (tl == NULL) return NULL;

    if (!expect_token(p, TK_RPAREN, "')'"))
        return NULL;

    AST_pair_patt pair = malloc(sizeof (*pair));
    pair->hd = hd;
    pair->tl = tl;

    AST_patt patt = malloc(sizeof (*patt));
    patt->type = PAIR_PATT;
    patt->obj.pair = pair;

    return patt;
}

/** expressions nodes **/

static AST_piece piece(Parser*, int n, ...);
static AST_expr expression(Parser*, Prec);
static AST_expr *expressions(Parser*, TK_type, TK_type, char*);

static AST_expr access_expr(Parser *p, AST_expr object) {
    next_token(p);  /* consume 'DOT' token */

    /* ignore any newlines inside of the expressions
       (e.g. "obj.field. <nl> inner_field ") */
    skip_newlines(p);
    
    Token *field = expect_token(p, TK_IDENT, "field name");
    if (field == NULL) return NULL;

    AST_access_expr access = malloc(sizeof (*access));
    access->field = ident_of_tok(field);
    access->object = object;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = ACCESS_EXPR;
    expr->obj.access = access;

    return expr;
}

static AST_expr assign_expr(Parser *p, AST_expr lvalue) {
    /* left side of '=' not an identifier */
    if (lvalue->type != IDENT_EXPR &&
        lvalue->type != INDEX_EXPR &&
        lvalue->type != ACCESS_EXPR) {
        reg_error(p, "invalid assignment target");
        return NULL;
    }

    next_token(p); /* consume '=' token */

    /* ignore any newlines inside of the expressions
       (e.g. "x = <nl> y = <nl> z ") */
    skip_newlines(p);

    /* LOW_PREC is the precedence below ASSIGN_PREC which allow
       multiple assign operators to nest to the right. */
    AST_expr value = expression(p, LOW_PREC);
    if (value == NULL) return NULL;

    AST_assign_expr assign = malloc(sizeof (*assign));
    assign->lvalue = lvalue;
    assign->value = value;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = ASSIGN_EXPR;
    expr->obj.assign = assign;

    return expr;
}

static AST_expr binary_expr(Parser *p, AST_expr left) {
    Token *op = next_token(p); /* the operator */

    /* ignore any newlines inside of the expression
       (e.g. "1 + <nl> 1") */
    skip_newlines(p);

    AST_expr right = expression(p, prec_of(op));
    if (right == NULL) return NULL;

    AST_binary_expr binary = malloc(sizeof (*binary));
    binary->op = op->type;
    binary->left = left;
    binary->right = right;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = BINARY_EXPR;
    expr->obj.binary = binary;

    return expr;
}

static AST_expr call_expr(Parser *p, AST_expr func) {
    next_token(p); /* consume '(' token */

    AST_expr *args = expressions(p, TK_COMMA, TK_RPAREN, "')'");
    if (args == NULL) return NULL;

    AST_call_expr call = malloc(sizeof (*call));
    call->func = func;
    call->args = args;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = CALL_EXPR;
    expr->obj.call = call;

    return expr;
}

static AST_expr cons_expr(Parser *p, AST_expr head) {
    next_token(p); /* consume '|' token */

    /* LCONS_PREC is the precedence below CONS_PREC which allow
       multiple cons operators to nest to the right. */
    AST_expr tail = expression(p, LCONS_PREC);
    if (tail == NULL) return NULL;
    
    AST_binary_expr binary = malloc(sizeof (*binary));
    binary->left = head;
    binary->right = tail;
    binary->op = TK_PIPE;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = BINARY_EXPR;
    expr->obj.binary = binary;

    return expr;
}

static AST_expr for_expr(Parser *p) {
    next_token(p);  /* consume 'for' token */

    AST_patt patt = pattern(p);
    if (patt == NULL) return NULL;

    if (!expect_token(p, TK_IN, "'in'"))
        return NULL;

    AST_expr iter = expression(p, LOW_PREC);
    if (iter == NULL) return NULL;

    if (!expect_token(p, TK_DO, "'do'"))
        return NULL;

    AST_piece body = piece(p, 1, TK_END);

    AST_for_expr for_expr = malloc(sizeof (*for_expr));
    for_expr->patt = patt;
    for_expr->iter = iter;
    for_expr->body = body;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = FOR_EXPR;
    expr->obj.for_expr = for_expr;

    return expr;
}

static AST_expr group_expr(Parser *p) {
    next_token(p);  /* consume '(' token */

    AST_expr gr_expr = expression(p, LOW_PREC);
    if (gr_expr == NULL) return NULL;

    if (!expect_token(p, TK_RPAREN, "')'"))
        return NULL;

    AST_group_expr group = malloc(sizeof (*group));
    group->expr = gr_expr;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = GROUP_EXPR;
    expr->obj.group = group;

    return expr;
}

static AST_expr identifier(Parser *p) {
    Token *ident = next_token(p);  /* the IDENT token */
    
    AST_expr expr = malloc(sizeof (*expr));
    expr->type = IDENT_EXPR;
    expr->obj.ident = ident_of_tok(ident);

    return expr;
}

static AST_elif elif_branch(Parser *p) {
    AST_expr cond = expression(p, LOW_PREC);
    if (cond == NULL) return NULL;

    if (!expect_token(p, TK_DO, "'do'"))
        return NULL;

    AST_piece then = piece(p, 3, TK_ELIF, TK_ELSE, TK_END);

    AST_elif elif = malloc(sizeof (*elif));
    elif->cond = cond;
    elif->then = then;

    return elif;
}

static AST_expr if_expr(Parser *p) {
    next_token(p);  /* consume 'if' token */

    AST_expr cond = expression(p, LOW_PREC);
    if (cond == NULL) return NULL;

    if (!expect_token(p, TK_DO, "'do'"))
        return NULL;

    /* this is an edge case, as the delimiter of the if body
       could be TK_ELSE, T_ELIF or TK_END.*/
    AST_piece then = piece(p, 3, TK_ELSE, TK_ELIF, TK_END);

    ARRAY(AST_elif) elifs;
    ARR_INIT(&elifs, AST_elif);
    
    while (prev_token_is(p, TK_ELIF)) {
        AST_elif elif = elif_branch(p);
        if (elif == NULL) return NULL;
            
        ARR_ADD(&elifs, elif);
    }
    ARR_ADD(&elifs, NULL);
    

    AST_piece alter = NULL;
    if (prev_token_is(p, TK_ELSE))
        alter = piece(p, 1, TK_END);

    AST_if_expr if_expr = malloc(sizeof (*if_expr));
    if_expr->cond = cond;
    if_expr->then = then;
    if_expr->elifs = elifs.elems;
    if_expr->alter = alter;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = IF_EXPR;
    expr->obj.if_expr = if_expr;

    return expr;
}

static AST_expr index_expr(Parser *p, AST_expr object) {
    next_token(p);  /* consume '[' token */

    AST_expr index = expression(p, LOW_PREC);
    if (index == NULL) return NULL;

    if (!expect_token(p, TK_RBRACKET, "]"))
        return NULL;

    AST_index_expr index_expr = malloc(sizeof (*index_expr));
    index_expr->object = object;
    index_expr->index = index;
    
    AST_expr expr = malloc(sizeof (*expr));
    expr->type = INDEX_EXPR;
    expr->obj.index = index_expr;

    return expr;
}

static AST_arm match_branch(Parser *p) {
    if (!expect_token(p, TK_DASH_GT, "'->'"))
        return NULL;

    AST_arm arm = NULL;
    if (match_token(p, TK_DO)) {
        AST_piece body = piece(p, 1, TK_END);
        
        arm = malloc(sizeof (*arm));
        arm->type = PIECE_ARM;
        arm->obj.p = body;
    } else {
        AST_expr expr = expression(p, LOW_PREC);
        if (expr == NULL) return NULL;
        
        arm = malloc(sizeof (*arm));
        arm->type = EXPR_ARM;
        arm->obj.e = expr;
    }
    
    return arm;
}

static AST_expr match_expr(Parser *p) {
    next_token(p);  /* consume 'match' token */

    AST_expr value = expression(p, LOW_PREC);
    if (value == NULL) return NULL;

    if (!expect_token(p, TK_DO, "do"))
        return NULL;

    skip_newlines(p); /* skip newlines after 'do' */

    ARRAY(AST_patt) patts;
    ARR_INIT(&patts, AST_patt);
    
    ARRAY(AST_arm) arms;
    ARR_INIT(&arms, AST_arm);
    
    while (match_token(p, TK_CASE)) {
        AST_patt patt = pattern(p);
        if (patt == NULL) return NULL;
        
        AST_arm branch = match_branch(p);
        if (branch == NULL) return NULL;

        ARR_ADD(&patts, patt);
        ARR_ADD(&arms, branch);
        skip_newlines(p);  /* skip newlines after case branch */
    }
    ARR_ADD(&patts, NULL);
    ARR_ADD(&arms, NULL);
    
    if (!expect_token(p, TK_END, "end"))
        return NULL;

    AST_match_expr match = malloc(sizeof (*match));
    match->value = value;
    match->patts = patts.elems;
    match->arms = arms.elems;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = MATCH_EXPR;
    expr->obj.match = match;

    return expr;
}

static AST_expr unary_expr(Parser *p) {
    Token *op = next_token(p);  /* the unary operator */

    AST_expr operand = expression(p, UNARY_PREC);
    if (operand == NULL) return NULL;

    AST_unary_expr unary = malloc(sizeof (*unary));
    unary->op = op->type;
    unary->operand = operand;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = UNARY_EXPR;
    expr->obj.unary = unary;

    return expr;
}

static AST_expr while_expr(Parser *p) {
    next_token(p);  /* consume 'while' token */

    AST_expr cond = expression(p, LOW_PREC);
    if (cond == NULL) return NULL;
        
    if (!expect_token(p, TK_DO, "do"))
        return NULL;
    
    AST_piece body = piece(p, 1, TK_END);

    AST_while_expr while_expr = malloc(sizeof (*while_expr));
    while_expr->cond = cond;
    while_expr->body = body;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = WHILE_EXPR;
    expr->obj.while_expr = while_expr;

    return expr;
}

/* for 'true, false and nil' literals */
static AST_expr fixed_literal(Parser *p) {
    AST_lit_expr lit = malloc(sizeof (*lit));
    
    if (curr_token_is(p, TK_FALSE))
        lit->type = FALSE_LIT;
    else if (curr_token_is(p, TK_TRUE))
        lit->type = TRUE_LIT;
    else
        lit->type = NIL_LIT;

    next_token(p);  /* consume the fixed */
    AST_expr expr = malloc(sizeof (*expr));
    expr->type = LIT_EXPR;
    expr->obj.lit = lit;

    return expr;
}

static AST_expr float_literal(Parser *p) {
    Token *tok = next_token(p);  /* float token */
    
    AST_lit_expr lit = malloc(sizeof (*lit));
    lit->type = FLOAT_LIT;
    lit->obj.f = float_of_tok(tok);
    
    AST_expr expr = malloc(sizeof (*expr));
    expr->type = LIT_EXPR;
    expr->obj.lit = lit;

    return expr;
}

static AST_expr fn_literal(Parser *p) {
    next_token(p); /* consume 'fn' token */

    if (!expect_token(p, TK_LPAREN, "("))
        return NULL;

    AST_patt *params = patterns(p, TK_COMMA, TK_RPAREN, ")");
    if (params == NULL) return NULL;

    AST_piece body = piece(p, 1, TK_END);
    
    AST_fn_lit fn = malloc(sizeof (*fn));
    fn->params = params;
    fn->body = body;

    AST_lit_expr lit = malloc(sizeof (*lit));
    lit->type = FN_LIT;
    lit->obj.fn = fn;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = LIT_EXPR;
    expr->obj.lit = lit;

    return expr;
}

static AST_expr hash_literal(Parser *p) {
    next_token(p);  /* consume '{' token */

    ARRAY(char*) names;
    ARR_INIT(&names, char*);
    
    ARRAY(AST_expr) values;
    ARR_INIT(&values, AST_expr);

    /* not an empty hash */
    if (!match_token(p, TK_RBRACE)) {
        skip_newlines(p);
        do {
            skip_newlines(p);
            Token *name = expect_token(p, TK_IDENT, "field name");
            if (name == NULL) return NULL;

            if (!expect_token(p, TK_COLON, ":"))
                return NULL;
        
            AST_expr value = expression(p, LOW_PREC);
            if (value == NULL) return NULL;

            ARR_ADD(&names, ident_of_tok(name));
            ARR_ADD(&values, value);
        } while (match_token(p, TK_COMMA));
        skip_newlines(p);
        
        if (!expect_token(p, TK_RBRACE, "}"))
            return NULL;
    }

    ARR_ADD(&names, NULL);
    ARR_ADD(&values, NULL);

    AST_hash_lit hash = malloc(sizeof (*hash));
    hash->names = names.elems;
    hash->values = values.elems;

    AST_lit_expr lit = malloc(sizeof (*lit));
    lit->type = HASH_LIT;
    lit->obj.hash = hash;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = LIT_EXPR;
    expr->obj.lit = lit;

    return expr;
}

static AST_expr int_literal(Parser *p) {
    Token *tok = next_token(p);  /* int token */
    int64_t i = int_of_tok(tok);
    
    AST_lit_expr lit = malloc(sizeof (*lit));
    lit->type = INT_LIT;
    lit->obj.i = i;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = LIT_EXPR;
    expr->obj.lit = lit;

    return expr;
}

static AST_expr list_literal(Parser *p) {
    next_token(p);  /* consume '[' token */

    AST_expr *values = expressions(p, TK_COMMA, TK_RBRACKET, "]");
    if (values == NULL) return NULL;

    AST_list_lit list = malloc(sizeof (*list));
    list->values = values;

    AST_lit_expr lit = malloc(sizeof (*lit));
    lit->type = LIST_LIT;
    lit->obj.list = list;

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = LIT_EXPR;
    expr->obj.lit = lit;

    return expr;
}

static AST_expr str_literal(Parser *p) {
    Token *tok = next_token(p);  /* str token */

    AST_lit_expr lit = malloc(sizeof (*lit));
    lit->type = tok->lexeme[0] == '`' ? RSTR_LIT : STR_LIT;
    lit->obj.s = str_of_tok(tok);

    AST_expr expr = malloc(sizeof (*expr));
    expr->type = LIT_EXPR;
    expr->obj.lit = lit;

    return expr;
}

/* return the prefix parse function of the token 'tok' */
static Prefix_F prefix_of(Token *tok) {
    switch (tok->type) {
    case TK_MINUS:
    case TK_NOT:
        return unary_expr;

    case TK_INT:
        return int_literal;
        
    case TK_FLOAT:
        return float_literal;

    case TK_RSTR:
    case TK_STR:
        return str_literal;
  
    case TK_FALSE:
    case TK_TRUE:
    case TK_NIL:
        return fixed_literal;

    case TK_FN:
        return fn_literal;

    case TK_LBRACKET:
        return list_literal;

    case TK_LBRACE:
        return hash_literal;

    case TK_IDENT:
        return identifier;

    case TK_LPAREN:
        return group_expr;

    case TK_IF:
        return if_expr;

    case TK_FOR:
        return for_expr;

    case TK_WHILE:
        return while_expr;
        
    case TK_MATCH:
        return match_expr;

    default:
        return NULL;
    }
}

/* return the infix parse function of the token 'tok' */
static Infix_F infix_of(Token *tok) {
    switch (tok->type) {
    case TK_PLUS:
    case TK_MINUS:
    case TK_ASTERISK:
    case TK_SLASH:
    case TK_PERCENT:
    case TK_LT:
    case TK_GT:
    case TK_EQ_EQ:
    case TK_BANG_EQ:
    case TK_LT_EQ:
    case TK_GT_EQ:
    case TK_AND:
    case TK_OR:
    case TK_AT:
        return binary_expr;

        /* '|' and '=' are right associative, so they have
           their own function instead of binary_expr */
    case TK_PIPE:
        return cons_expr;
    case TK_EQ:
        return assign_expr;
        
    case TK_LPAREN:
        return call_expr;

    case TK_LBRACKET:
        return index_expr;
        
    case TK_DOT:
        return access_expr;

    default:
        return NULL;
    }
}

/** statements nodes **/

/*
  ::NOTE::
  Statements parse functions return NULL on an error,
  so 'piece' function can notice that and synchronize
  the tokens to the next statement skipping any tokens
  came after the token that cause the parse error.
*/

static AST_stmt expr_stmt(Parser *p) {
    AST_expr expr = expression(p, LOW_PREC);
    if (expr == NULL) return NULL;

    AST_expr_stmt expr_stmt = malloc(sizeof (*expr_stmt));
    expr_stmt->expr = expr;
    
    AST_stmt stmt = malloc(sizeof (*stmt));
    stmt->type = EXPR_STMT;
    stmt->obj.expr = expr_stmt;

    return stmt;
}

static AST_stmt fn_stmt(Parser *p) {
    next_token(p); /* consume 'fn' token */

    Token *name = expect_token(p, TK_IDENT, "name");
    if (name == NULL) return NULL;
    
    if (!expect_token(p, TK_LPAREN, "'('"))
        return NULL;

    AST_patt *params = patterns(p, TK_COMMA, TK_RPAREN, ")");
    if (params == NULL) return NULL;

    AST_piece body = piece(p, 1, TK_END);

    AST_fn_stmt fn = malloc(sizeof (*fn));
    fn->name = ident_of_tok(name);
    fn->params = params;
    fn->body = body;
    
    AST_stmt stmt = malloc(sizeof (*stmt));
    stmt->type = FN_STMT;
    stmt->obj.fn = fn;

    return stmt;
}

static AST_stmt let_stmt(Parser *p) {
    next_token(p);  /* consume 'let' token */
    
    AST_patt patt = pattern(p);
    if (patt == NULL) return NULL;

    if (!expect_token(p, TK_EQ, "'='"))
        return NULL;

    AST_expr expr = expression(p, LOW_PREC);
    if (expr == NULL) return NULL;

    AST_let_stmt let = malloc(sizeof (*let));
    let->patt = patt;
    let->value = expr;
    
    AST_stmt stmt = malloc(sizeof (*stmt));
    stmt->type = LET_STMT;
    stmt->obj.let = let;

    return stmt;
}

static AST_stmt ret_stmt(Parser *p) {
    next_token(p); /* consume 'return' token */

    AST_expr expr = expression(p, LOW_PREC);
    if (expr == NULL) return NULL;

    AST_ret_stmt ret = malloc(sizeof (*ret));
    ret->value = expr;
    
    AST_stmt stmt = malloc(sizeof (*stmt));
    stmt->type = RET_STMT;
    stmt->obj.ret = ret;

    return stmt;
}

static AST_stmt fixed_stmt(Parser *p) {
    Token *fixed = next_token(p); /* consume fixed keyword */
    
    AST_stmt stmt = malloc(sizeof (*stmt));
    stmt->type = FIXED_STMT;
    stmt->obj.fixed = fixed->type;
    
    return stmt;
}

/** main nodes **/

static AST_patt pattern(Parser *p) {
    Token *curr = curr_token(p);
    AST_patt patt;
    
    switch(curr->type) {
    case TK_LBRACE:
        patt = hash_patt(p);
        break;

    case TK_LBRACKET:
        patt = list_patt(p);
        break;

    case TK_LPAREN:
        patt = pair_patt(p);
        break;

    case TK_IDENT:
        patt = ident_patt(p);
        break;

    case TK_STR:
    case TK_INT:
    case TK_FLOAT:
        patt = const_patt(p);
        break;

    default:
        reg_error(p, "invalid pattern");
        return NULL;
    }

    if (patt != NULL)
        patt->where = curr; /* pattern location (by token) */

    return patt;
}

/* return an array of zero or more AST_patt delimited by 
   'dl' token type and ended witn 'end' token type */
static AST_patt*
patterns(Parser *p, TK_type dl, TK_type end, char *end_name) {
    ARRAY(AST_patt) patts;
    ARR_INIT(&patts, AST_patt);
    
    AST_patt patt;
    if (!match_token(p, end)) {
        do {
            skip_newlines(p);
            patt = pattern(p);
            
            if (patt == NULL)
                return NULL;
            
            ARR_ADD(&patts, patt);
        } while(match_token(p, dl));
        
        skip_newlines(p);
        if (!expect_token(p, end, end_name))
            return NULL;
    }
    ARR_ADD(&patts, NULL);
    return patts.elems;
}

static AST_expr expression(Parser *p, Prec prec) {
    Token *curr = curr_token(p);
    Prefix_F prefix = prefix_of(curr);
    
    if (prefix == NULL) {
        reg_error(p, "unexpected symbol");
        return NULL;
    }

    AST_expr expr = prefix(p);

    if (expr != NULL)
        expr->where = curr;  /* expression location (by token) */

    while (!at_end(p) &&
           !curr_token_is(p, TK_NL) &&
           !curr_token_is(p, TK_SEMICOLON) &&
           prec < prec_of(curr_token(p))) {
        
        Infix_F infix = infix_of(curr_token(p));
        
        if (infix == NULL)
            return expr;

        expr = infix(p, expr);

        if (expr == NULL)
            return NULL;
    }
    
    return expr;
}

/* return an array of zero or more AST_expr delimited by 
   'dl' token type and ended witn 'end' token type */
static AST_expr *
expressions(Parser *p, TK_type dl, TK_type end, char *end_name) {
    ARRAY(AST_expr) exprs;
    ARR_INIT(&exprs, AST_expr);
    AST_expr expr;

    if (!match_token(p, end)) {
        do {
            skip_newlines(p);
            
            expr = expression(p, LOW_PREC);
            if (expr == NULL)
                return NULL;
            
            ARR_ADD(&exprs, expr);
        } while (match_token(p, dl));
        
        skip_newlines(p);
        if (!expect_token(p, end, end_name))
            return NULL;
    }

    ARR_ADD(&exprs, NULL);
    return exprs.elems;
}

static AST_stmt statement(Parser *p, int n, va_list ap) {
    AST_stmt stmt;
    Token *curr = curr_token(p);
        
    switch (curr->type) {
    case TK_FN:
        /* check first if it's 'fn_stmt' and not a 'fn_literal' */
        if (peek_token_is(p, TK_IDENT))
            stmt = fn_stmt(p);
        else
            stmt = expr_stmt(p);
        break;
            
    case TK_LET:
        stmt = let_stmt(p);
        break;
        
    case TK_RETURN:
        stmt = ret_stmt(p);
        break;
        
    case TK_CONTINUE:
    case TK_BREAK:
        stmt = fixed_stmt(p);
        break;
        
    default:
        stmt = expr_stmt(p);
        break;
    }

    /* if there is no error and not the end of the block */
    if (stmt != NULL && curr_token_not(p, n, ap)) {
        /* check for newline or semicolon, otherwise report an error */
        if (!curr_token_is(p, TK_NL) &&
            !curr_token_is(p, TK_SEMICOLON)) {
            reg_error(p, "expect ';' or newline after statement");
            return NULL;
        }
        next_token(p);         /* if ';' or newline, consume it */
        stmt->where = curr;   /* statement location (by token) */
    }

    return stmt;
}

/* syncronize the parser token list to the start of the next statement */
static void sync(Parser *p) {
    while (!at_end(p)                     &&
           !curr_token_is(p, TK_FN)       &&
           !curr_token_is(p, TK_LET)      &&
           !curr_token_is(p, TK_RETURN)   &&
           !curr_token_is(p, TK_CONTINUE) &&
           !curr_token_is(p, TK_BREAK)) {
        next_token(p);
    }
}

/* parse a block of statements until TK_EOF token or any
   token specified in the variable length argument list */
static AST_piece piece(Parser *p, int n, ...) {
    va_list ap;
    ARRAY(AST_stmt) stmts;
    ARR_INIT(&stmts, AST_stmt);

    /* ignore any newlines before the beginning of the block */
    skip_newlines(p);

    va_start(ap, n);
    while (!at_end(p) && curr_token_not(p, n, ap)) {
        /* reset the argument list for 'statement' function */
        va_start(ap, n);
        AST_stmt stmt = statement(p, n, ap);
        
        if (stmt != NULL) {
            ARR_ADD(&stmts, stmt);
        } else {
            /* if error occur discard any tokens left from the
               current statement, as they will produce meaningless
               error messages. */
            sync(p);
        }

        /* reset the argument list for the new loop */
        va_start(ap, n);

        /* ignore any newlines occur before the next statement */
        skip_newlines(p);
    }

    va_start(ap, n);
    if (curr_token_not(p, n, ap)) {
        reg_error(p, "'end' expected");
        return NULL;
    }
    next_token(p);  /* consume end token */
    va_end(ap);
    
    /* terminate the array */
    ARR_ADD(&stmts, NULL);
    
    AST_piece piece = malloc(sizeof(AST_piece));
    piece->stmts = stmts.elems;
    return piece;
}

/*** INTERFACE ***/

void init_parser(Parser *parser, Token *tokens) {
    parser->tokens = tokens;

    parser->curr = &tokens[0];
    parser->peek = &tokens[1];
    parser->prev = NULL;

    parser->been_error = 0;
    ARR_INIT(&parser->errors, SErr);
}

void free_parser(Parser *parser) {
    parser->tokens = NULL;
    parser->curr = NULL;
    parser->peek = NULL;
    parser->prev = NULL;

    ARR_FREE(&parser->errors);
}

AST_piece parse_piece(Parser *parser) {
    return piece(parser, 1, TK_EOF);
}

AST_stmt parse_stmt(Parser *parser) {
    return statement(parser, 0, NULL);
}

AST_expr parse_expr(Parser *parser) {
    return expression(parser, LOW_PREC);
}

AST_patt parse_patt(Parser *parser) {
    return pattern(parser);
}
