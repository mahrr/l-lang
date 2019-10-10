/*
 * (debug.c | 12 Mar 19 | Ahmad Maher, Kareem Hamdy)
 *
*/

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#include "ast.h"
#include "list.h"
#include "object.h"
#include "token.h"

/** INTERNALS **/

/* string representation of each token type */
char *tok_types_str[] = {
    /* Literals */
    "INT", "FLOAT", "STR", "RSTR",
    "FALSE", "TRUE", "NIL",
    
    /* Keywords */
    "FN", "RETURN", "LET", "TYPE",
    "DO", "END", "IF", "ELIF", "ELSE",
    "FOR", "WHILE", "CONTINUE",
    "BREAK", "COND", "MATCH", "CASE",
    
    /* Identefier */
    "IDENT",
    
    /* Operators */
    "AND", "OR", "NOT",
    ".", "@", "|",
    
    /* Arthimetik Operators */
    "+", "-", "*", "/", "%",
    
    /* Ordering Operators */
    "<", ">", "==",
    "!=", "<=", ">=",
    
    /* Delimiters */
    "LPAREN", "RPAREN",
    "LBRACE", "RBRACE",
    "LBRACKET", "RBRACKET",
    "COMMA", "DASH_GT",
    "COLON", "SEMICOLON",
    "EQ", "IN", "NL",
    
    /* Errors and end of file */
    "ERR", "EOF",
};

/* the current identation level for the ast printer */
static int indent_level = -1;
static char *indent = "  ";

#define INDENT()                             \
    for (int i = 0; i < indent_level; i++)   \
        fputs(indent, stdout);

static void print_expr(AST_expr);
static void print_exprs(AST_expr*);
static void print_patt(AST_patt);
static void print_patts(AST_patt*);

void print_piece(AST_piece);

/* print an operator 'op' with arbitrary number 'n' 
   of expressions in s-expression form */
static void paren_op(char *op, int n, ...) {
    va_list ap;
    va_start(ap, n);
    
    printf("(%s", op);
    AST_expr e;
    for (int i = 0; i < n; i++) {
        putchar(' ');
        e = va_arg(ap, AST_expr);
        print_expr(e);
    }
    printf(")");

    va_end(ap);
}

/* print an operator(or keyword) with it's associated block
   in an s-expression like form */
static void paren_block(char *op, AST_expr cond, AST_piece body) {
    printf("|%s| ", op);
    if (cond != NULL)
        print_expr(cond);
    putchar('\n');
    print_piece(body);
}

/* print hash key */
static void print_key(AST_key key) {
    if (key->type == EXPR_KEY) {
        putchar('[');
        print_expr(key->expr);
        printf("]:");  
    } else if (key->type == SYMBOL_KEY) {
        printf("%s:", key->symbol);
    } else {
        fprintf(stderr, "[INTERNAL] invalid key type (%d)\n", key->type);
        assert(0);
    }
}

static void print_lit_expr(AST_lit_expr e) {
    switch (e->type) {

    case FN_LIT:
        printf("(fn [");
        print_patts(e->fn->params);
        printf("]\n");
        print_piece(e->fn->body);
        printf(")");
        break;

    case HASH_LIT: {
        AST_hash_lit hash = e->hash;
        printf("(hash ");
        
        AST_key *keys = hash->keys;
        AST_expr *values = hash->values;
        for (int i = 0; keys[i]; i++) {
            print_key(keys[i]);
            print_expr(values[i]);
            putchar(' ');
        }
        printf(")");
        break;
    }

    case LIST_LIT:
        printf("(list");
        print_exprs(e->list->values);
        printf(")");
        break;

    case FALSE_LIT:
        printf("false");
        break;
        
    case FLOAT_LIT:
        printf("%f", e->f);
        break;
        
    case INT_LIT:
        printf("%ld", e->i);
        break;

    case NIL_LIT:
        printf("nil");
        break;

    case STR_LIT:
        printf("'%s'", e->s);
        break;

    case RSTR_LIT:
        printf("`%s`", e->s);
        break;

    case TRUE_LIT:
        printf("true");
        break;
    }

    fprintf(stderr, "[INTERNAL] invalid literal type (%d)", e->type);
    assert(0);
}

static void print_patt(AST_patt p) {
    switch (p->type) {
        
    case BOOL_CPATT:
        printf(p->b ? "true" : "false");
        break;
        
    case CONS_PATT:
        printf("(:cons ");
        print_expr(p->cons->tag);
        print_patts(p->cons->patts);
        putchar(')');
        break;

    case HASH_PATT: {
        AST_hash_patt hash = p->hash;
        printf("(:hash ");
        
        AST_key *keys = hash->keys;
        AST_patt *patts = hash->patts;
        for (int i = 0; keys[i]; i++) {
            print_key(keys[i]);
            print_patt(patts[i]);
            putchar(' ');
        }
        
        printf(")");
        break;
    }

    case LIST_PATT:
        printf("(:list ");
        print_patts(p->list->patts);
        printf(")");
        break;

    case PAIR_PATT:
        printf("(:pair ");
        print_patt(p->pair->hd);
        printf(" | ");
        print_patt(p->pair->tl);
        printf(")");
        break;
    
    case FLOAT_CPATT:
        printf("%f", p->f);
        break;

    case IDENT_PATT:
        printf("%s", p->ident);
        break;

    case INT_CPATT:
        printf("%ld", p->i);
        break;

    case NIL_CPATT:
        printf("nil");
        break;

    case STR_CPATT:
        printf("'%s'", p->s);
        break;
    }

    fprintf(stderr, "[INTERNAL] invalid pattern type (%d)\n", p->type);
    assert(0);
}

static void print_patts(AST_patt *patts) {
    for (int i = 0; patts[i]; i++) {
        print_patt(patts[i]);
        putchar(' ');
    }
}

static void print_expr(AST_expr e) {
    switch (e->type) {

    case ASSIGN_EXPR: {
        AST_assign_expr assign = e->assign;
        paren_op("=", 2, assign->lvalue, assign->value);
        break;
    }

    case BINARY_EXPR: {
        AST_binary_expr bin = e->binary;
        paren_op(tok_types_str[bin->op],
                     2, bin->left, bin->right);
        break;
    }

    case CALL_EXPR:
        putchar('(');
        print_expr(e->call->func);
        print_exprs(e->call->args);
        putchar(')');
        break;

    case COND_EXPR: {
        printf("|cond| ");
        putchar('\n');

        AST_expr *exprs = e->cond->exprs;
        AST_arm *arms = e->cond->arms;
        
        for (int i = 0; arms[i]; i++) {
            indent_level++;
            INDENT();
            print_expr(exprs[i]);
            printf(" -> ");

            if (arms[i]->type == EXPR_ARM) {
                print_expr(arms[i]->e);
                putchar('\n');
            } else {
                putchar('\n');
                print_piece(arms[i]->p);
            }
           
            indent_level--;
        }
        break;
    }

    case FOR_EXPR:
        printf("|for| ");
        print_patt(e->for_expr->patt);
        printf(" in ");
        print_expr(e->for_expr->iter);
        putchar('\n');
        print_piece(e->for_expr->body);
        break;

    case GROUP_EXPR:
        paren_op("GR", 1, e->group->expr);
        break;

    case IF_EXPR: {
        AST_if_expr if_expr = e->if_expr;
        paren_block("if", if_expr->cond, if_expr->then);

        AST_elif *elifs = if_expr->elifs;
        for (int i = 0; elifs[i]; i++)
            paren_block("elif", elifs[i]->cond, elifs[i]->then);
        
        if (if_expr->alter != NULL)
            paren_block("else", NULL, if_expr->alter);
        break;
    }

    case IDENT_EXPR:
        printf("%s", e->ident);
        break;

    case INDEX_EXPR:
        paren_op("[]", 2, e->index->object, e->index->index);
        break;

    case LIT_EXPR:
        print_lit_expr(e->lit);
        break;

    case MATCH_EXPR: {
        AST_match_expr match = e->match;
        printf("|match| ");
        print_expr(match->value);
        putchar('\n');

        AST_patt *patts = match->patts;
        AST_arm *arms = match->arms;
        
        for (int i = 0; arms[i]; i++) {
            indent_level++;
            INDENT();
            print_patt(patts[i]);
            printf(" -> ");

            if (arms[i]->type == EXPR_ARM) {
                print_expr(arms[i]->e);
                putchar('\n');
            } else {
                putchar('\n');
                print_piece(arms[i]->p);
            }
            indent_level--;
        }
                
        break;
    }

    case UNARY_EXPR:
        paren_op(tok_types_str[e->unary->op],
                     1, e->unary->operand);
        break;

    case WHILE_EXPR: {
        AST_while_expr wh = e->while_expr;
        paren_block("|while|", wh->cond, wh->body);
        break;
    }
        
    }

    fprintf(stderr, "[INTERNAL] invalid expression type (%d)\n", e->type);
    assert(0);
}

static void print_exprs(AST_expr *exprs) {
    for (int i = 0; exprs[i]; i++) {
        putchar(' ');
        print_expr(exprs[i]);
        putchar(' ');
    }
}

static void print_variants(AST_variant *variants) {
    for (int i = 0; variants[i]; i++) {
        printf("  (%s:", variants[i]->name);
        for (int j = 0; j < variants[i]->count; j++)
            printf(" %s", variants[i]->params[j]);
        printf(")\n");
    }
}

static void print_stmt(AST_stmt s) {
    switch (s->type) {
        
    case EXPR_STMT:
        printf("{");
        print_expr(s->expr->expr);
        printf("}\n");
        break;
        
    case FN_STMT:
        printf("{fn (%s) [", s->fn->name);
        print_patts(s->fn->params);
        printf("]\n");
        print_piece(s->fn->body);
        printf("}\n");
        break;

    case LET_STMT:
        printf("{let ");
        print_patt(s->let->patt);
        putchar(' ');
        print_expr(s->let->value);
        printf("}\n");
        break;

    case RET_STMT:
        printf("{ret ");
        print_expr(s->ret->value);
        printf("}\n");
        break;

    case TYPE_STMT:
        printf("{type %s\n", s->type_stmt->name);
        print_variants(s->type_stmt->variants);
        printf("}\n");
        break;

    case FIXED_STMT:
        printf("{%s}\n", tok_types_str[s->fixed]);
        break;
    }

    fprintf(stderr, "[INTERNAL] invalid statement type (%d)\n", s->type);
    assert(0);
}

/** INTEFACE **/

void print_token(Token *t) {
    printf("[%s @line %ld] %.*s (%d) : %s\n",
           t->file,
           t->line,
           t->length,
           t->lexeme,
           t->length,
           tok_types_str[t->type]);
}

void print_piece(AST_piece p) {
    indent_level++;
    for (int i = 0; p->stmts[i]; i++) {
        INDENT();
        print_stmt(p->stmts[i]);
    }
    indent_level--;
}

void inspect(Rav_obj *obj);

static void inspect_table(Table *table, char *name) {
    if (!table)
        return;

    printf("%.7s table has %d elements\n", name, table->elems);
    return;
}

static void inspect_hash(Hash_obj *hash) {
    /* floats */
    printf("value: <hash>, type: Hash\n");
    if (hash->array) {
        printf("table array has %d (%d) elements\n",
               hash->array->len, hash->array->cap);
    }
    inspect_table(hash->float_table, "floats");
    inspect_table(hash->int_table, "ints");
    inspect_table(hash->str_table, "str");
    inspect_table(hash->obj_table, "objects");
}

static void inspect_list(List *list) {
    printf("[\n");
    while (list) {
        inspect(list->head);
        list = list->tail;
    }
    printf("],\ntype: List\n");
}

static void inspect_variant(Variant_obj *variant) {
    printf("%s(\n", variant->cons->cn->name);
    for (int i = 0; i < variant->count; i++)
        inspect(variant->elems[i]);
    printf("),\ntype: Variant of %s\n", variant->cons->cn->type);
}

void inspect(Rav_obj *obj) {
    switch (obj->type) {
    case BOOL_OBJ:
        printf("value: %d, type: Boolean\n", obj->b);
        break;

    case BLTIN_OBJ:
        printf("value:<fn>(-%d-), type: Builtin\n", obj->bl->arity);
        break;

    case CLOS_OBJ:
        printf("value:<fn>(-%d-), type: CLosure\n", obj->cl->arity);
        break;

    case CONS_OBJ:
        printf("value: %s(-%d-), type: Constructor of %s\n",
               obj->cn->name, obj->cn->arity, obj->cn->type);
        break;

    case FLOAT_OBJ:
        printf("value: %f, type: Float\n", obj->f);
        break;
        
    case HASH_OBJ:
        inspect_hash(obj->h);
        break;

    case LIST_OBJ:
        inspect_list(obj->l);
        break;

    case INT_OBJ:
        printf("value: %ld, type: Int\n", obj->i);
        break;

    case NIL_OBJ:
        printf("value: nil, type: Nil\n");
        break;

    case STR_OBJ:
        printf("value: '%s', type: String\n", obj->s);
        break;

    case VARI_OBJ:
        inspect_variant(obj->vr);
        break;

    case VOID_OBJ:
        printf("value: (), type: Void\n");
        break;
    }
    
    fprintf(stderr, "[INTERNAL] invalid Rav_Type (%d)", obj->type);
    assert(0);
}
