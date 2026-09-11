#ifndef DYNPHONY_FRONTEND_H
#define DYNPHONY_FRONTEND_H

enum DynTokenKind {
    DYN_TOK_EOF,
    DYN_TOK_INT,
    DYN_TOK_VOID,
    DYN_TOK_MAIN,
    DYN_TOK_RETURN,
    DYN_TOK_NUMBER,
    DYN_TOK_LPAREN,
    DYN_TOK_RPAREN,
    DYN_TOK_LBRACE,
    DYN_TOK_RBRACE,
    DYN_TOK_SEMICOLON,
    DYN_TOK_PLUS,
    DYN_TOK_MINUS,
    DYN_TOK_STAR,
    DYN_TOK_SLASH,
    DYN_TOK_PERCENT,
    DYN_TOK_LSHIFT,
    DYN_TOK_RSHIFT,
    DYN_TOK_AMP,
    DYN_TOK_PIPE,
    DYN_TOK_CARET,
    DYN_TOK_TILDE,
    DYN_TOK_INVALID
};

struct DynToken {
    int kind;
    unsigned int value;
    unsigned int position;
};

struct DynLexer {
    const char *source;
    unsigned int length;
    unsigned int position;
    struct DynToken current;
    int error;
};

enum DynNodeKind {
    DYN_NODE_NUMBER,
    DYN_NODE_POSITIVE,
    DYN_NODE_NEGATIVE,
    DYN_NODE_NOT,
    DYN_NODE_ADD,
    DYN_NODE_SUBTRACT,
    DYN_NODE_MULTIPLY,
    DYN_NODE_DIVIDE,
    DYN_NODE_REMAINDER,
    DYN_NODE_LSHIFT,
    DYN_NODE_RSHIFT,
    DYN_NODE_AND,
    DYN_NODE_OR,
    DYN_NODE_XOR
};

struct DynNode {
    int kind;
    unsigned int value;
    unsigned int left;
    unsigned int right;
};

struct DynAstProgram {
    struct DynNode *nodes;
    unsigned int count;
    unsigned int capacity;
    unsigned int expression;
};

void dyn_lexer_init(
    struct DynLexer *lexer,
    const char *source,
    unsigned int length
);
void dyn_lexer_next(struct DynLexer *lexer);
int dyn_parse(
    const char *source,
    unsigned int length,
    struct DynAstProgram *program
);
int dyn_evaluate(
    const struct DynAstProgram *program,
    unsigned int node,
    unsigned int *result
);

#endif
