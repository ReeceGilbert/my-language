#ifndef AST_H
#define AST_H

#include "token.h"

typedef enum {
    AST_MODULE,
    AST_BLOCK,

    AST_EXPR_STMT,
    AST_ASSIGN_STMT,
    AST_RETURN_STMT,
    AST_IF_STMT,
    AST_FOR_STMT,
    AST_WHILE_STMT,
    AST_FUNCTION_DEF,
    AST_CLASS_DEF,
    AST_PASS_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,

    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_LITERAL_EXPR,
    AST_IDENTIFIER_EXPR,
    AST_GROUPING_EXPR,
    AST_CALL_EXPR,
    AST_MEMBER_EXPR
} AstNodeType;

typedef struct AstNode AstNode;

typedef struct {
    AstNode** items;
    int count;
    int capacity;
} AstNodeArray;

typedef struct {
    Token* items;
    int count;
    int capacity;
} TokenList;

struct AstNode {
    AstNodeType type;
    Token token;

    union {
        struct {
            AstNodeArray statements;
        } module;

        struct {
            AstNodeArray statements;
        } block;

        struct {
            AstNode* expression;
        } exprStmt;

        struct {
            AstNode* target;
            AstNode* value;
        } assignStmt;

        struct {
            AstNode* value;
        } returnStmt;

        struct {
            AstNode* condition;
            AstNode* thenBlock;
            AstNode* elseBranch;
        } ifStmt;

        struct {
            AstNode* target;
            AstNode* iterable;
            AstNode* body;
        } forStmt;

        struct {
            AstNode* condition;
            AstNode* body;
        } whileStmt;

        struct {
            Token name;
            TokenList parameters;
            AstNode* body;
        } functionDef;

        struct {
            Token name;
            AstNode* body;
        } classDef;

        struct {
            Token op;
            AstNode* left;
            AstNode* right;
        } binaryExpr;

        struct {
            Token op;
            AstNode* operand;
        } unaryExpr;

        struct {
            Token literal;
        } literalExpr;

        struct {
            Token name;
        } identifierExpr;

        struct {
            AstNode* expression;
        } groupingExpr;

        struct {
            AstNode* callee;
            AstNodeArray arguments;
        } callExpr;

        struct {
            AstNode* object;
            Token member;
        } memberExpr;
    } as;
};

void initAstNodeArray(AstNodeArray* array);
void pushAstNode(AstNodeArray* array, AstNode* node);

void initTokenList(TokenList* list);
void pushTokenList(TokenList* list, Token token);

AstNode* newAstNode(AstNodeType type, Token token);

AstNode* makeBinaryNode(Token op, AstNode* left, AstNode* right);
AstNode* makeUnaryNode(Token op, AstNode* operand);
AstNode* makeIdentifierNode(Token name);
AstNode* makeLiteralNode(Token literal);

void printAst(const AstNode* node, int depth);

void freeAst(AstNode* node);
void freeAstNodeArray(AstNodeArray* array);
void freeTokenList(TokenList* list);

#endif