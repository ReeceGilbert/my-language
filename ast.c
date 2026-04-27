#include <stdio.h>
#include <stdlib.h>

#include "ast.h"

#define AST_ARRAY_INITIAL_CAPACITY 8
#define TOKEN_LIST_INITIAL_CAPACITY 8

static void printIndent(int depth);
static void printTokenLexeme(Token token);

void initAstNodeArray(AstNodeArray* array) {
    if (array == NULL) {
        return;
    }

    array->items = NULL;
    array->count = 0;
    array->capacity = 0;
}

void pushAstNode(AstNodeArray* array, AstNode* node) {
    AstNode** newItems;
    int newCapacity;

    if (array == NULL) {
        return;
    }

    if (array->count >= array->capacity) {
        newCapacity = (array->capacity < AST_ARRAY_INITIAL_CAPACITY)
            ? AST_ARRAY_INITIAL_CAPACITY
            : array->capacity * 2;

        newItems = (AstNode**)realloc(array->items, sizeof(AstNode*) * newCapacity);

        if (newItems == NULL) {
            fprintf(stderr, "Out of memory while growing AST node array.\n");
            exit(1);
        }

        array->items = newItems;
        array->capacity = newCapacity;
    }

    array->items[array->count++] = node;
}

void freeAstNodeArray(AstNodeArray* array) {
    int i;

    if (array == NULL) {
        return;
    }

    for (i = 0; i < array->count; i++) {
        freeAst(array->items[i]);
    }

    free(array->items);
    array->items = NULL;
    array->count = 0;
    array->capacity = 0;
}

void initTokenList(TokenList* list) {
    if (list == NULL) {
        return;
    }

    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void pushTokenList(TokenList* list, Token token) {
    Token* newItems;
    int newCapacity;

    if (list == NULL) {
        return;
    }

    if (list->count >= list->capacity) {
        newCapacity = (list->capacity < TOKEN_LIST_INITIAL_CAPACITY)
            ? TOKEN_LIST_INITIAL_CAPACITY
            : list->capacity * 2;

        newItems = (Token*)realloc(list->items, sizeof(Token) * newCapacity);

        if (newItems == NULL) {
            fprintf(stderr, "Out of memory while growing token list.\n");
            exit(1);
        }

        list->items = newItems;
        list->capacity = newCapacity;
    }

    list->items[list->count++] = token;
}

void freeTokenList(TokenList* list) {
    if (list == NULL) {
        return;
    }

    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

AstNode* newAstNode(AstNodeType type, Token token) {
    AstNode* node = (AstNode*)calloc(1, sizeof(AstNode));

    if (node == NULL) {
        fprintf(stderr, "Out of memory while allocating AST node.\n");
        exit(1);
    }

    node->type = type;
    node->token = token;
    return node;
}

AstNode* makeBinaryNode(Token op, AstNode* left, AstNode* right) {
    AstNode* node = newAstNode(AST_BINARY_EXPR, op);

    node->as.binaryExpr.op = op;
    node->as.binaryExpr.left = left;
    node->as.binaryExpr.right = right;

    return node;
}

AstNode* makeUnaryNode(Token op, AstNode* operand) {
    AstNode* node = newAstNode(AST_UNARY_EXPR, op);

    node->as.unaryExpr.op = op;
    node->as.unaryExpr.operand = operand;

    return node;
}

AstNode* makeIdentifierNode(Token name) {
    AstNode* node = newAstNode(AST_IDENTIFIER_EXPR, name);

    node->as.identifierExpr.name = name;

    return node;
}

AstNode* makeLiteralNode(Token literal) {
    AstNode* node = newAstNode(AST_LITERAL_EXPR, literal);

    node->as.literalExpr.literal = literal;

    return node;
}

void freeAst(AstNode* node) {
    if (node == NULL) {
        return;
    }

    switch (node->type) {
        case AST_MODULE:
            freeAstNodeArray(&node->as.module.statements);
            break;

        case AST_BLOCK:
            freeAstNodeArray(&node->as.block.statements);
            break;

        case AST_EXPR_STMT:
            freeAst(node->as.exprStmt.expression);
            break;

        case AST_ASSIGN_STMT:
            freeAst(node->as.assignStmt.target);
            freeAst(node->as.assignStmt.value);
            break;

        case AST_RETURN_STMT:
            freeAst(node->as.returnStmt.value);
            break;

        case AST_IF_STMT:
            freeAst(node->as.ifStmt.condition);
            freeAst(node->as.ifStmt.thenBlock);
            freeAst(node->as.ifStmt.elseBranch);
            break;

        case AST_FOR_STMT:
            freeAst(node->as.forStmt.target);
            freeAst(node->as.forStmt.iterable);
            freeAst(node->as.forStmt.body);
            break;

        case AST_WHILE_STMT:
            freeAst(node->as.whileStmt.condition);
            freeAst(node->as.whileStmt.body);
            break;

        case AST_FUNCTION_DEF:
            freeTokenList(&node->as.functionDef.parameters);
            freeAst(node->as.functionDef.body);
            break;

        case AST_CLASS_DEF:
            freeAst(node->as.classDef.body);
            break;

        case AST_PASS_STMT:
        case AST_BREAK_STMT:
        case AST_CONTINUE_STMT:
            break;

        case AST_BINARY_EXPR:
            freeAst(node->as.binaryExpr.left);
            freeAst(node->as.binaryExpr.right);
            break;

        case AST_UNARY_EXPR:
            freeAst(node->as.unaryExpr.operand);
            break;

        case AST_LITERAL_EXPR:
        case AST_IDENTIFIER_EXPR:
            break;

        case AST_GROUPING_EXPR:
            freeAst(node->as.groupingExpr.expression);
            break;

        case AST_CALL_EXPR:
            freeAst(node->as.callExpr.callee);
            freeAstNodeArray(&node->as.callExpr.arguments);
            break;

        case AST_MEMBER_EXPR:
            freeAst(node->as.memberExpr.object);
            break;

        case AST_LIST_EXPR:
            freeAstNodeArray(&node->as.listExpr.elements);
            break;

        case AST_INDEX_EXPR:
            freeAst(node->as.indexExpr.object);
            freeAst(node->as.indexExpr.index);
            break;
    }

    free(node);
}

void printAst(const AstNode* node, int depth) {
    int i;

    if (node == NULL) {
        printIndent(depth);
        printf("<null>\n");
        return;
    }

    switch (node->type) {
        case AST_MODULE:
            printIndent(depth);
            printf("MODULE\n");

            for (i = 0; i < node->as.module.statements.count; i++) {
                printAst(node->as.module.statements.items[i], depth + 1);
            }
            break;

        case AST_BLOCK:
            printIndent(depth);
            printf("BLOCK\n");

            for (i = 0; i < node->as.block.statements.count; i++) {
                printAst(node->as.block.statements.items[i], depth + 1);
            }
            break;

        case AST_EXPR_STMT:
            printIndent(depth);
            printf("EXPR_STMT\n");
            printAst(node->as.exprStmt.expression, depth + 1);
            break;

        case AST_ASSIGN_STMT:
            printIndent(depth);
            printf("ASSIGN\n");
            printAst(node->as.assignStmt.target, depth + 1);
            printAst(node->as.assignStmt.value, depth + 1);
            break;

        case AST_RETURN_STMT:
            printIndent(depth);
            printf("RETURN\n");
            printAst(node->as.returnStmt.value, depth + 1);
            break;

        case AST_IF_STMT:
            printIndent(depth);
            printf("IF\n");

            printIndent(depth + 1);
            printf("COND\n");
            printAst(node->as.ifStmt.condition, depth + 2);

            printIndent(depth + 1);
            printf("THEN\n");
            printAst(node->as.ifStmt.thenBlock, depth + 2);

            if (node->as.ifStmt.elseBranch != NULL) {
                printIndent(depth + 1);
                printf("ELSE\n");
                printAst(node->as.ifStmt.elseBranch, depth + 2);
            }
            break;

        case AST_FOR_STMT:
            printIndent(depth);
            printf("FOR\n");

            printIndent(depth + 1);
            printf("TARGET\n");
            printAst(node->as.forStmt.target, depth + 2);

            printIndent(depth + 1);
            printf("ITERABLE\n");
            printAst(node->as.forStmt.iterable, depth + 2);

            printIndent(depth + 1);
            printf("BODY\n");
            printAst(node->as.forStmt.body, depth + 2);
            break;

        case AST_WHILE_STMT:
            printIndent(depth);
            printf("WHILE\n");

            printIndent(depth + 1);
            printf("COND\n");
            printAst(node->as.whileStmt.condition, depth + 2);

            printIndent(depth + 1);
            printf("BODY\n");
            printAst(node->as.whileStmt.body, depth + 2);
            break;

        case AST_FUNCTION_DEF:
            printIndent(depth);
            printf("FUNCTION ");
            printTokenLexeme(node->as.functionDef.name);
            printf("(");

            for (i = 0; i < node->as.functionDef.parameters.count; i++) {
                if (i > 0) {
                    printf(", ");
                }

                printTokenLexeme(node->as.functionDef.parameters.items[i]);
            }

            printf(")\n");
            printAst(node->as.functionDef.body, depth + 1);
            break;

        case AST_CLASS_DEF:
            printIndent(depth);
            printf("CLASS ");
            printTokenLexeme(node->as.classDef.name);
            printf("\n");
            printAst(node->as.classDef.body, depth + 1);
            break;

        case AST_PASS_STMT:
            printIndent(depth);
            printf("PASS\n");
            break;

        case AST_BREAK_STMT:
            printIndent(depth);
            printf("BREAK\n");
            break;

        case AST_CONTINUE_STMT:
            printIndent(depth);
            printf("CONTINUE\n");
            break;

        case AST_BINARY_EXPR:
            printIndent(depth);
            printf("BINARY ");
            printTokenLexeme(node->as.binaryExpr.op);
            printf("\n");

            printAst(node->as.binaryExpr.left, depth + 1);
            printAst(node->as.binaryExpr.right, depth + 1);
            break;

        case AST_UNARY_EXPR:
            printIndent(depth);
            printf("UNARY ");
            printTokenLexeme(node->as.unaryExpr.op);
            printf("\n");

            printAst(node->as.unaryExpr.operand, depth + 1);
            break;

        case AST_LITERAL_EXPR:
            printIndent(depth);
            printf("LITERAL ");
            printTokenLexeme(node->as.literalExpr.literal);
            printf("\n");
            break;

        case AST_IDENTIFIER_EXPR:
            printIndent(depth);
            printf("IDENT ");
            printTokenLexeme(node->as.identifierExpr.name);
            printf("\n");
            break;

        case AST_GROUPING_EXPR:
            printIndent(depth);
            printf("GROUP\n");
            printAst(node->as.groupingExpr.expression, depth + 1);
            break;

        case AST_CALL_EXPR:
            printIndent(depth);
            printf("CALL\n");

            printIndent(depth + 1);
            printf("CALLEE\n");
            printAst(node->as.callExpr.callee, depth + 2);

            printIndent(depth + 1);
            printf("ARGS\n");

            for (i = 0; i < node->as.callExpr.arguments.count; i++) {
                printAst(node->as.callExpr.arguments.items[i], depth + 2);
            }
            break;

        case AST_MEMBER_EXPR:
            printIndent(depth);
            printf("MEMBER ");
            printTokenLexeme(node->as.memberExpr.member);
            printf("\n");

            printAst(node->as.memberExpr.object, depth + 1);
            break;

        case AST_LIST_EXPR:
            printIndent(depth);
            printf("LIST\n");

            for (i = 0; i < node->as.listExpr.elements.count; i++) {
                printAst(node->as.listExpr.elements.items[i], depth + 1);
            }
            break;

        case AST_INDEX_EXPR:
            printIndent(depth);
            printf("INDEX\n");

            printIndent(depth + 1);
            printf("OBJECT\n");
            printAst(node->as.indexExpr.object, depth + 2);

            printIndent(depth + 1);
            printf("INDEX_VALUE\n");
            printAst(node->as.indexExpr.index, depth + 2);
            break;
    }
}

static void printIndent(int depth) {
    while (depth-- > 0) {
        printf("  ");
    }
}

static void printTokenLexeme(Token token) {
    printf("%.*s", token.length, token.start);
}