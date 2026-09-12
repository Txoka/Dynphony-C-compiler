#ifndef DYNPHONY_FRONTEND_H
#define DYNPHONY_FRONTEND_H

enum DynTokenKind {
    DYN_TOK_EOF,
    DYN_TOK_INT,
    DYN_TOK_VOID,
    DYN_TOK_MAIN,
    DYN_TOK_RETURN,
    DYN_TOK_IDENTIFIER,
    DYN_TOK_NUMBER,
    DYN_TOK_STRING,
    DYN_TOK_CHAR,
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
    DYN_TOK_BANG,
    DYN_TOK_QUESTION,
    DYN_TOK_COLON,
    DYN_TOK_COMMA,
    DYN_TOK_ASSIGN,
    DYN_TOK_ADD_ASSIGN,
    DYN_TOK_SUB_ASSIGN,
    DYN_TOK_MUL_ASSIGN,
    DYN_TOK_DIV_ASSIGN,
    DYN_TOK_REM_ASSIGN,
    DYN_TOK_AND_ASSIGN,
    DYN_TOK_OR_ASSIGN,
    DYN_TOK_XOR_ASSIGN,
    DYN_TOK_LSHIFT_ASSIGN,
    DYN_TOK_RSHIFT_ASSIGN,
    DYN_TOK_EQUAL,
    DYN_TOK_NOT_EQUAL,
    DYN_TOK_LESS,
    DYN_TOK_GREATER,
    DYN_TOK_LESS_EQUAL,
    DYN_TOK_GREATER_EQUAL,
    DYN_TOK_LOGICAL_AND,
    DYN_TOK_LOGICAL_OR,
    DYN_TOK_PLUS_PLUS,
    DYN_TOK_MINUS_MINUS,
    DYN_TOK_ARROW,
    DYN_TOK_LBRACKET,
    DYN_TOK_RBRACKET,
    DYN_TOK_DOT,
    DYN_TOK_UNSIGNED,
    DYN_TOK_SIGNED,
    DYN_TOK_CHAR_TYPE,
    DYN_TOK_SHORT,
    DYN_TOK_LONG,
    DYN_TOK_STRUCT,
    DYN_TOK_ENUM,
    DYN_TOK_TYPEDEF,
    DYN_TOK_STATIC,
    DYN_TOK_EXTERN,
    DYN_TOK_CONST,
    DYN_TOK_SIZEOF,
    DYN_TOK_IF,
    DYN_TOK_ELSE,
    DYN_TOK_WHILE,
    DYN_TOK_DO,
    DYN_TOK_FOR,
    DYN_TOK_BREAK,
    DYN_TOK_CONTINUE,
    DYN_TOK_INVALID
};

struct DynToken {
    int kind;
    unsigned int value;
    unsigned int position;
    unsigned int length;
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
    DYN_NODE_XOR,
    DYN_NODE_LOGICAL_NOT,
    DYN_NODE_ADDRESS,
    DYN_NODE_DEREFERENCE,
    DYN_NODE_SUBSCRIPT,
    DYN_NODE_MEMBER,
    DYN_NODE_EQUAL,
    DYN_NODE_NOT_EQUAL,
    DYN_NODE_LESS,
    DYN_NODE_GREATER,
    DYN_NODE_LESS_EQUAL,
    DYN_NODE_GREATER_EQUAL,
    DYN_NODE_LOGICAL_AND,
    DYN_NODE_LOGICAL_OR,
    DYN_NODE_CONDITIONAL,
    DYN_NODE_COMMA,
    DYN_NODE_LOCAL,
    DYN_NODE_GLOBAL,
    DYN_NODE_ASSIGN,
    DYN_NODE_POST_INCREMENT,
    DYN_NODE_CALL_INPUT,
    DYN_NODE_CALL_OUTPUT,
    DYN_NODE_CALL_INTRINSIC,
    DYN_NODE_CALL,
    DYN_NODE_ARGUMENT,
    DYN_NODE_SEQUENCE,
    DYN_NODE_RETURN,
    DYN_NODE_IF,
    DYN_NODE_WHILE,
    DYN_NODE_FOR,
    DYN_NODE_DO,
    DYN_NODE_BREAK,
    DYN_NODE_CONTINUE,
    DYN_NODE_EXPRESSION
};

struct DynNode {
    int kind;
    unsigned int value;
    unsigned int left;
    unsigned int right;
    unsigned int extra;
    unsigned int dimension_start;
    unsigned int dimension_count;
};

struct DynLocal {
    unsigned int position;
    unsigned int length;
    unsigned int size;
    unsigned int element_size;
    unsigned int count;
    unsigned int offset;
    int array;
    int pointer;
    unsigned int struct_id;
    unsigned int dimension_start;
    unsigned int dimension_count;
    unsigned int scope_depth;
};

struct DynFunction {
    unsigned int name_position;
    unsigned int name_length;
    unsigned int body;
    unsigned int local_base;
    unsigned int local_count;
    unsigned int parameter_count;
    unsigned int frame_size;
};

struct DynGlobal {
    unsigned int name_position;
    unsigned int name_length;
    unsigned int size;
    unsigned int element_size;
    unsigned int count;
    unsigned int initial_value;
    unsigned int initializer_node;
    int array;
    int pointer;
    unsigned int struct_id;
    unsigned int dimension_start;
    unsigned int dimension_count;
    int defined;
    int internal;
    char *data;
    unsigned int data_length;
};

struct DynConstant {
    unsigned int name_position;
    unsigned int name_length;
    unsigned int value;
};

struct DynTypeAlias {
    unsigned int name_position;
    unsigned int name_length;
    unsigned int size;
    unsigned int element_size;
    unsigned int struct_id;
    int pointer;
};

struct DynStruct {
    unsigned int name_position;
    unsigned int name_length;
    unsigned int size;
    unsigned int alignment;
    unsigned int member_start;
    unsigned int member_count;
    int defined;
};

struct DynMember {
    unsigned int name_position;
    unsigned int name_length;
    unsigned int offset;
    unsigned int size;
    unsigned int element_size;
    unsigned int struct_id;
    unsigned int dimension_start;
    unsigned int dimension_count;
    int pointer;
    int array;
};

struct DynDimension {
    unsigned int count;
    unsigned int stride;
};

struct DynAstProgram {
    struct DynNode *nodes;
    unsigned int count;
    unsigned int capacity;
    unsigned int expression;
    const char *source;
    struct DynLocal *locals;
    unsigned int local_count;
    unsigned int local_capacity;
    struct DynFunction *functions;
    unsigned int function_count;
    unsigned int function_capacity;
    unsigned int main_function;
    struct DynGlobal *globals;
    unsigned int global_count;
    unsigned int global_capacity;
    struct DynConstant *constants;
    unsigned int constant_count;
    unsigned int constant_capacity;
    struct DynTypeAlias *aliases;
    unsigned int alias_count;
    unsigned int alias_capacity;
    struct DynStruct *structs;
    unsigned int struct_count;
    unsigned int struct_capacity;
    struct DynMember *members;
    unsigned int member_count;
    unsigned int member_capacity;
    struct DynDimension *dimensions;
    unsigned int dimension_count;
    unsigned int dimension_capacity;
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
