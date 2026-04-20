#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "token_shared.h"

// ============================================================
// PARSER FOR THE EXISTING PYTHON-LIKE LEXER
// Separate file meant to sit beside your lexer.
// This file assumes Token, TokenType, TokenArray, and tokenTypeToString()
// are already defined exactly like in your lexer.
// ============================================================

// ------------------------------------------------------------
// AST TYPES
// ------------------------------------------------------------

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
            AstNode* elseBranch; // either block or nested if
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

// ------------------------------------------------------------
// PARSER STATE
// ------------------------------------------------------------

typedef struct {
    TokenArray* tokens;
    int current;
    int panicMode;
    int hadError;
} ParseState;

// ------------------------------------------------------------
// DYNAMIC ARRAY HELPERS
// ------------------------------------------------------------

static void initAstNodeArray(AstNodeArray* array) {
    array->items = NULL;
    array->count = 0;
    array->capacity = 0;
}

static void pushAstNode(AstNodeArray* array, AstNode* node) {
    if (array->count >= array->capacity) {
        int newCapacity = (array->capacity < 8) ? 8 : array->capacity * 2;
        AstNode** newItems = (AstNode**)realloc(array->items, sizeof(AstNode*) * newCapacity);
        if (newItems == NULL) {
            fprintf(stderr, "Out of memory while growing AST node array.\n");
            exit(1);
        }
        array->items = newItems;
        array->capacity = newCapacity;
    }
    array->items[array->count++] = node;
}

static void initTokenList(TokenList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void pushTokenList(TokenList* list, Token token) {
    if (list->count >= list->capacity) {
        int newCapacity = (list->capacity < 8) ? 8 : list->capacity * 2;
        Token* newItems = (Token*)realloc(list->items, sizeof(Token) * newCapacity);
        if (newItems == NULL) {
            fprintf(stderr, "Out of memory while growing token list.\n");
            exit(1);
        }
        list->items = newItems;
        list->capacity = newCapacity;
    }
    list->items[list->count++] = token;
}

// ------------------------------------------------------------
// AST HELPERS
// ------------------------------------------------------------

static AstNode* newAstNode(AstNodeType type, Token token) {
    AstNode* node = (AstNode*)calloc(1, sizeof(AstNode));
    if (node == NULL) {
        fprintf(stderr, "Out of memory while allocating AST node.\n");
        exit(1);
    }
    node->type = type;
    node->token = token;
    return node;
}

// ------------------------------------------------------------
// PARSER CORE HELPERS
// ------------------------------------------------------------

static void initParseState(ParseState* parser, TokenArray* tokens) {
    parser->tokens = tokens;
    parser->current = 0;
    parser->panicMode = 0;
    parser->hadError = 0;
}

static Token* parserPeek(ParseState* parser) {
    return &parser->tokens->data[parser->current];
}

static Token* parserPrevious(ParseState* parser) {
    if (parser->current <= 0) return NULL;
    return &parser->tokens->data[parser->current - 1];
}

static int parserIsAtEnd(ParseState* parser) {
    return parserPeek(parser)->type == TOKEN_EOF;
}

static Token* parserAdvance(ParseState* parser) {
    if (!parserIsAtEnd(parser)) parser->current++;
    return parserPrevious(parser);
}

static int parserCheck(ParseState* parser, TokenType type) {
    if (parserIsAtEnd(parser)) return type == TOKEN_EOF;
    return parserPeek(parser)->type == type;
}

static int parserMatch(ParseState* parser, TokenType type) {
    if (!parserCheck(parser, type)) return 0;
    parserAdvance(parser);
    return 1;
}

static void parserErrorAtToken(ParseState* parser, Token* token, const char* message) {
    parser->hadError = 1;

    if (parser->panicMode) return;
    parser->panicMode = 1;

    fprintf(stderr, "[parse error] line %d col %d near '%.*s': %s\n",
            token->line,
            token->column,
            token->length,
            token->start,
            message);
}

static Token* parserConsume(ParseState* parser, TokenType type, const char* message) {
    if (parserCheck(parser, type)) return parserAdvance(parser);
    parserErrorAtToken(parser, parserPeek(parser), message);
    return NULL;
}

static void parserSynchronize(ParseState* parser) {
    parser->panicMode = 0;

    // Always consume at least one token so we cannot get stuck
    // re-reporting the same error forever.
    if (!parserIsAtEnd(parser)) {
        parserAdvance(parser);
    }

    while (!parserIsAtEnd(parser)) {
        Token* prev = parserPrevious(parser);
        if (prev && prev->type == TOKEN_NEWLINE) return;

        switch (parserPeek(parser)->type) {
            case TOKEN_DEF:
            case TOKEN_CLASS:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_RETURN:
            case TOKEN_PASS:
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
                return;
            default:
                break;
        }

        parserAdvance(parser);
    }
}

static void skipNewlines(ParseState* parser) {
    while (parserMatch(parser, TOKEN_NEWLINE)) {
    }
}

// ------------------------------------------------------------
// FORWARD DECLARATIONS
// ------------------------------------------------------------

static AstNode* parseDeclaration(ParseState* parser);
static AstNode* parseStatement(ParseState* parser);
static AstNode* parseBlock(ParseState* parser);
static AstNode* parseExpression(ParseState* parser);
static AstNode* parseAssignment(ParseState* parser);
static AstNode* parseOr(ParseState* parser);
static AstNode* parseAnd(ParseState* parser);
static AstNode* parseEquality(ParseState* parser);
static AstNode* parseComparison(ParseState* parser);
static AstNode* parseTerm(ParseState* parser);
static AstNode* parseFactor(ParseState* parser);
static AstNode* parseUnary(ParseState* parser);
static AstNode* parseCall(ParseState* parser);
static AstNode* parsePrimary(ParseState* parser);

// ------------------------------------------------------------
// AST BUILDERS
// ------------------------------------------------------------

static AstNode* makeBinaryNode(Token op, AstNode* left, AstNode* right) {
    AstNode* node = newAstNode(AST_BINARY_EXPR, op);
    node->as.binaryExpr.op = op;
    node->as.binaryExpr.left = left;
    node->as.binaryExpr.right = right;
    return node;
}

static AstNode* makeUnaryNode(Token op, AstNode* operand) {
    AstNode* node = newAstNode(AST_UNARY_EXPR, op);
    node->as.unaryExpr.op = op;
    node->as.unaryExpr.operand = operand;
    return node;
}

static AstNode* makeIdentifierNode(Token name) {
    AstNode* node = newAstNode(AST_IDENTIFIER_EXPR, name);
    node->as.identifierExpr.name = name;
    return node;
}

static AstNode* makeLiteralNode(Token literal) {
    AstNode* node = newAstNode(AST_LITERAL_EXPR, literal);
    node->as.literalExpr.literal = literal;
    return node;
}

// ------------------------------------------------------------
// PROGRAM / DECLARATIONS
// ------------------------------------------------------------

static AstNode* parseModule(ParseState* parser) {
    AstNode* module = newAstNode(AST_MODULE, *parserPeek(parser));
    initAstNodeArray(&module->as.module.statements);

    skipNewlines(parser);

    while (!parserIsAtEnd(parser)) {
        AstNode* stmt = parseDeclaration(parser);
        if (stmt != NULL) {
            pushAstNode(&module->as.module.statements, stmt);
        } else {
            parserSynchronize(parser);
        }
        skipNewlines(parser);
    }

    return module;
}

static AstNode* parseFunctionDef(ParseState* parser) {
    Token* defTok = parserConsume(parser, TOKEN_DEF, "Expected 'def'.");
    Token* name = parserConsume(parser, TOKEN_IDENTIFIER, "Expected function name.");
    AstNode* node;

    if (defTok == NULL || name == NULL) return NULL;

    node = newAstNode(AST_FUNCTION_DEF, *defTok);
    node->as.functionDef.name = *name;
    initTokenList(&node->as.functionDef.parameters);

    if (parserConsume(parser, TOKEN_LPAREN, "Expected '(' after function name.") == NULL) return NULL;

    if (!parserCheck(parser, TOKEN_RPAREN)) {
        do {
            Token* param = parserConsume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
            if (param == NULL) return NULL;
            pushTokenList(&node->as.functionDef.parameters, *param);
        } while (parserMatch(parser, TOKEN_COMMA));
    }

    if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after parameter list.") == NULL) return NULL;
    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after function signature.") == NULL) return NULL;
    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after function signature.") == NULL) return NULL;

    node->as.functionDef.body = parseBlock(parser);
    return node;
}

static AstNode* parseClassDef(ParseState* parser) {
    Token* classTok = parserConsume(parser, TOKEN_CLASS, "Expected 'class'.");
    Token* name = parserConsume(parser, TOKEN_IDENTIFIER, "Expected class name.");
    AstNode* node;

    if (classTok == NULL || name == NULL) return NULL;

    node = newAstNode(AST_CLASS_DEF, *classTok);
    node->as.classDef.name = *name;

    if (parserMatch(parser, TOKEN_LPAREN)) {
        if (!parserCheck(parser, TOKEN_RPAREN)) {
            if (parseExpression(parser) == NULL) return NULL;
            while (parserMatch(parser, TOKEN_COMMA)) {
                if (parseExpression(parser) == NULL) return NULL;
            }
        }
        if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after base list.") == NULL) return NULL;
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after class name.") == NULL) return NULL;
    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after class header.") == NULL) return NULL;

    node->as.classDef.body = parseBlock(parser);
    return node;
}

static AstNode* parseDeclaration(ParseState* parser) {
    if (parserCheck(parser, TOKEN_DEF)) return parseFunctionDef(parser);
    if (parserCheck(parser, TOKEN_CLASS)) return parseClassDef(parser);
    return parseStatement(parser);
}

// ------------------------------------------------------------
// BLOCKS / STATEMENTS
// ------------------------------------------------------------

static AstNode* parseBlock(ParseState* parser) {
    AstNode* block;

    if (parserConsume(parser, TOKEN_INDENT, "Expected indented block.") == NULL) return NULL;

    block = newAstNode(AST_BLOCK, *parserPrevious(parser));
    initAstNodeArray(&block->as.block.statements);

    skipNewlines(parser);

    while (!parserCheck(parser, TOKEN_DEDENT) && !parserCheck(parser, TOKEN_EOF)) {
        AstNode* stmt = parseDeclaration(parser);
        if (stmt != NULL) {
            pushAstNode(&block->as.block.statements, stmt);
        } else {
            parserSynchronize(parser);
        }
        skipNewlines(parser);
    }

    if (parserConsume(parser, TOKEN_DEDENT, "Expected end of indented block.") == NULL) return NULL;
    return block;
}

static AstNode* parseReturnStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_RETURN, "Expected 'return'.");
    AstNode* node;
    if (tok == NULL) return NULL;

    node = newAstNode(AST_RETURN_STMT, *tok);

    if (parserCheck(parser, TOKEN_NEWLINE) ||
        parserCheck(parser, TOKEN_DEDENT) ||
        parserCheck(parser, TOKEN_EOF)) {
        node->as.returnStmt.value = NULL;
        } else {
            node->as.returnStmt.value = parseExpression(parser);
            if (node->as.returnStmt.value == NULL) return NULL;
        }

    if (!parserMatch(parser, TOKEN_NEWLINE) &&
        !parserCheck(parser, TOKEN_DEDENT) &&
        !parserCheck(parser, TOKEN_EOF)) {
        parserErrorAtToken(parser, parserPeek(parser),
                           "Expected newline after return statement.");
        return NULL;
        }

    return node;
}

static AstNode* parsePassStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_PASS, "Expected 'pass'.");
    AstNode* node;
    if (tok == NULL) return NULL;

    node = newAstNode(AST_PASS_STMT, *tok);

    if (!parserMatch(parser, TOKEN_NEWLINE) &&
        !parserCheck(parser, TOKEN_DEDENT) &&
        !parserCheck(parser, TOKEN_EOF)) {
        parserErrorAtToken(parser, parserPeek(parser),
                           "Expected newline after pass.");
        return NULL;
        }

    return node;
}

static AstNode* parseBreakStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_BREAK, "Expected 'break'.");
    AstNode* node;
    if (tok == NULL) return NULL;

    node = newAstNode(AST_BREAK_STMT, *tok);

    if (!parserMatch(parser, TOKEN_NEWLINE) &&
        !parserCheck(parser, TOKEN_DEDENT) &&
        !parserCheck(parser, TOKEN_EOF)) {
        parserErrorAtToken(parser, parserPeek(parser),
                           "Expected newline after break.");
        return NULL;
        }

    return node;
}

static AstNode* parseContinueStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_CONTINUE, "Expected 'continue'.");
    AstNode* node;
    if (tok == NULL) return NULL;

    node = newAstNode(AST_CONTINUE_STMT, *tok);

    if (!parserMatch(parser, TOKEN_NEWLINE) &&
        !parserCheck(parser, TOKEN_DEDENT) &&
        !parserCheck(parser, TOKEN_EOF)) {
        parserErrorAtToken(parser, parserPeek(parser),
                           "Expected newline after continue.");
        return NULL;
        }

    return node;
}

static AstNode* parseIfLikeStatement(ParseState* parser, int startedWithElif) {
    Token* tok;
    AstNode* node;

    if (startedWithElif) {
        tok = parserConsume(parser, TOKEN_ELIF, "Expected 'elif'.");
    } else {
        tok = parserConsume(parser, TOKEN_IF, "Expected 'if'.");
    }

    if (tok == NULL) return NULL;

    node = newAstNode(AST_IF_STMT, *tok);
    node->as.ifStmt.condition = parseExpression(parser);
    if (node->as.ifStmt.condition == NULL) return NULL;

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after if condition.") == NULL) return NULL;
    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after if header.") == NULL) return NULL;

    node->as.ifStmt.thenBlock = parseBlock(parser);
    if (node->as.ifStmt.thenBlock == NULL) return NULL;
    node->as.ifStmt.elseBranch = NULL;

    skipNewlines(parser);

    if (parserCheck(parser, TOKEN_ELIF)) {
        node->as.ifStmt.elseBranch = parseIfLikeStatement(parser, 1);
        return node;
    }

    if (parserMatch(parser, TOKEN_ELSE)) {
        if (parserConsume(parser, TOKEN_COLON, "Expected ':' after else.") == NULL) return NULL;
        if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after else header.") == NULL) return NULL;
        node->as.ifStmt.elseBranch = parseBlock(parser);
    }

    return node;
}

static AstNode* parseIfStatement(ParseState* parser) {
    return parseIfLikeStatement(parser, 0);
}

static AstNode* parseWhileStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_WHILE, "Expected 'while'.");
    AstNode* node;
    if (tok == NULL) return NULL;

    node = newAstNode(AST_WHILE_STMT, *tok);
    node->as.whileStmt.condition = parseExpression(parser);
    if (node->as.whileStmt.condition == NULL) return NULL;

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after while condition.") == NULL) return NULL;
    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after while header.") == NULL) return NULL;

    node->as.whileStmt.body = parseBlock(parser);
    return node;
}

static AstNode* parseExpressionStatement(ParseState* parser) {
    AstNode* expr = parseExpression(parser);
    AstNode* stmt;

    if (expr == NULL) return NULL;

    if (!parserMatch(parser, TOKEN_NEWLINE) && !parserCheck(parser, TOKEN_EOF)) {
        parserErrorAtToken(parser, parserPeek(parser),
                           "Expected newline after statement.");
        return NULL;
    }

    // Assignment nodes are already statements.
    if (expr->type == AST_ASSIGN_STMT) {
        return expr;
    }

    stmt = newAstNode(AST_EXPR_STMT, expr->token);
    stmt->as.exprStmt.expression = expr;
    return stmt;
}

static AstNode* parseForStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_FOR, "Expected 'for'.");
    Token* name;
    AstNode* node;
    AstNode* target;
    AstNode* iterable;

    if (tok == NULL) return NULL;

    name = parserConsume(parser, TOKEN_IDENTIFIER, "Expected loop variable after 'for'.");
    if (name == NULL) return NULL;

    if (parserConsume(parser, TOKEN_IN, "Expected 'in' after loop variable.") == NULL) {
        return NULL;
    }

    iterable = parseExpression(parser);
    if (iterable == NULL) return NULL;

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after for loop header.") == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after for loop header.") == NULL) {
        return NULL;
    }

    node = newAstNode(AST_FOR_STMT, *tok);
    target = makeIdentifierNode(*name);
    node->as.forStmt.target = target;
    node->as.forStmt.iterable = iterable;
    node->as.forStmt.body = parseBlock(parser);
    return node;
}

static AstNode* parseStatement(ParseState* parser) {
    if (parserCheck(parser, TOKEN_IF)) return parseIfStatement(parser);
    if (parserCheck(parser, TOKEN_WHILE)) return parseWhileStatement(parser);
    if (parserCheck(parser, TOKEN_RETURN)) return parseReturnStatement(parser);
    if (parserCheck(parser, TOKEN_PASS)) return parsePassStatement(parser);
    if (parserCheck(parser, TOKEN_BREAK)) return parseBreakStatement(parser);
    if (parserCheck(parser, TOKEN_CONTINUE)) return parseContinueStatement(parser);
    if (parserCheck(parser, TOKEN_FOR)) return parseForStatement(parser);
    return parseExpressionStatement(parser);
}

// ------------------------------------------------------------
// EXPRESSIONS
// ------------------------------------------------------------

static AstNode* parseExpression(ParseState* parser) {
    return parseAssignment(parser);
}

static AstNode* parseAssignment(ParseState* parser) {
    AstNode* left = parseOr(parser);

    if (left == NULL) return NULL;

    if (parserMatch(parser, TOKEN_EQUAL)) {
        Token op = *parserPrevious(parser);
        AstNode* value = parseAssignment(parser);
        AstNode* node;

        if (value == NULL) return NULL;

        if (left->type != AST_IDENTIFIER_EXPR && left->type != AST_MEMBER_EXPR) {
            parserErrorAtToken(parser, &op, "Invalid assignment target.");
            return NULL;
        }

        node = newAstNode(AST_ASSIGN_STMT, op);
        node->as.assignStmt.target = left;
        node->as.assignStmt.value = value;
        return node;
    }

    return left;
}

static AstNode* parseOr(ParseState* parser) {
    AstNode* expr = parseAnd(parser);
    while (parserMatch(parser, TOKEN_OR)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseAnd(parser);
        if (right == NULL) return NULL;
        expr = makeBinaryNode(op, expr, right);
    }
    return expr;
}

static AstNode* parseAnd(ParseState* parser) {
    AstNode* expr = parseEquality(parser);
    while (parserMatch(parser, TOKEN_AND)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseEquality(parser);
        if (right == NULL) return NULL;
        expr = makeBinaryNode(op, expr, right);
    }
    return expr;
}

static AstNode* parseEquality(ParseState* parser) {
    AstNode* expr = parseComparison(parser);
    while (parserMatch(parser, TOKEN_EQUAL_EQUAL) || parserMatch(parser, TOKEN_NOT_EQUAL)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseComparison(parser);
        if (right == NULL) return NULL;
        expr = makeBinaryNode(op, expr, right);
    }
    return expr;
}

static AstNode* parseComparison(ParseState* parser) {
    AstNode* expr = parseTerm(parser);
    while (parserMatch(parser, TOKEN_LESS) ||
           parserMatch(parser, TOKEN_LESS_EQUAL) ||
           parserMatch(parser, TOKEN_GREATER) ||
           parserMatch(parser, TOKEN_GREATER_EQUAL)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseTerm(parser);
        if (right == NULL) return NULL;
        expr = makeBinaryNode(op, expr, right);
    }
    return expr;
}

static AstNode* parseTerm(ParseState* parser) {
    AstNode* expr = parseFactor(parser);
    while (parserMatch(parser, TOKEN_PLUS) || parserMatch(parser, TOKEN_MINUS)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseFactor(parser);
        if (right == NULL) return NULL;
        expr = makeBinaryNode(op, expr, right);
    }
    return expr;
}

static AstNode* parseFactor(ParseState* parser) {
    AstNode* expr = parseUnary(parser);
    while (parserMatch(parser, TOKEN_STAR) ||
           parserMatch(parser, TOKEN_SLASH) ||
           parserMatch(parser, TOKEN_PERCENT)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseUnary(parser);
        if (right == NULL) return NULL;
        expr = makeBinaryNode(op, expr, right);
    }
    return expr;
}

static AstNode* parseUnary(ParseState* parser) {
    if (parserMatch(parser, TOKEN_NOT) || parserMatch(parser, TOKEN_MINUS) || parserMatch(parser, TOKEN_PLUS)) {
        Token op = *parserPrevious(parser);
        AstNode* operand = parseUnary(parser);
        if (operand == NULL) return NULL;
        return makeUnaryNode(op, operand);
    }

    return parseCall(parser);
}

static AstNode* parseCall(ParseState* parser) {
    AstNode* expr = parsePrimary(parser);

    if (expr == NULL) return NULL;

    for (;;) {
        if (parserMatch(parser, TOKEN_LPAREN)) {
            AstNode* call = newAstNode(AST_CALL_EXPR, *parserPrevious(parser));
            initAstNodeArray(&call->as.callExpr.arguments);
            call->as.callExpr.callee = expr;

            if (!parserCheck(parser, TOKEN_RPAREN)) {
                do {
                    AstNode* arg = parseExpression(parser);
                    if (arg == NULL) return NULL;
                    pushAstNode(&call->as.callExpr.arguments, arg);
                } while (parserMatch(parser, TOKEN_COMMA));
            }

            if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after arguments.") == NULL) return NULL;
            expr = call;
            continue;
        }

        if (parserMatch(parser, TOKEN_DOT)) {
            Token* member = parserConsume(parser, TOKEN_IDENTIFIER, "Expected member name after '.'.");
            AstNode* access;
            if (member == NULL) return NULL;
            access = newAstNode(AST_MEMBER_EXPR, *member);
            access->as.memberExpr.object = expr;
            access->as.memberExpr.member = *member;
            expr = access;
            continue;
        }

        break;
    }

    return expr;
}

static AstNode* parsePrimary(ParseState* parser) {
    if (parserMatch(parser, TOKEN_NUMBER) ||
        parserMatch(parser, TOKEN_STRING) ||
        parserMatch(parser, TOKEN_TRUE) ||
        parserMatch(parser, TOKEN_FALSE) ||
        parserMatch(parser, TOKEN_NONE)) {
        return makeLiteralNode(*parserPrevious(parser));
    }

    if (parserMatch(parser, TOKEN_IDENTIFIER)) {
        return makeIdentifierNode(*parserPrevious(parser));
    }

    if (parserMatch(parser, TOKEN_LPAREN)) {
        Token open = *parserPrevious(parser);
        AstNode* expr = parseExpression(parser);
        AstNode* grouping;
        if (expr == NULL) return NULL;
        if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after expression.") == NULL) return NULL;
        grouping = newAstNode(AST_GROUPING_EXPR, open);
        grouping->as.groupingExpr.expression = expr;
        return grouping;
    }

    parserErrorAtToken(parser, parserPeek(parser), "Expected expression.");
    return NULL;
}

// ------------------------------------------------------------
// DEBUG PRINTING
// ------------------------------------------------------------

static void printIndent(int depth) {
    while (depth-- > 0) printf("  ");
}

static void printTokenLexeme(Token token) {
    printf("%.*s", token.length, token.start);
}

void printAst(AstNode* node, int depth) {
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
            if (node->as.ifStmt.elseBranch) {
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
            printAst(node->as.whileStmt.condition, depth + 1);
            printAst(node->as.whileStmt.body, depth + 1);
            break;

        case AST_FUNCTION_DEF:
            printIndent(depth);
            printf("FUNCTION ");
            printTokenLexeme(node->as.functionDef.name);
            printf("(");
            for (i = 0; i < node->as.functionDef.parameters.count; i++) {
                if (i > 0) printf(", ");
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
    }
}

// ------------------------------------------------------------
// PUBLIC ENTRY POINT
// ------------------------------------------------------------

AstNode* parseTokens(TokenArray* tokens) {
    ParseState parser;
    AstNode* root;

    initParseState(&parser, tokens);
    root = parseModule(&parser);

    if (parser.hadError) {
        fprintf(stderr, "Parsing finished with errors.\n");
    }

    return root;
}

/*
USAGE INSIDE YOUR CURRENT MAIN AFTER LEXING:

    AstNode* root = parseTokens(&tokens);
    printf("\n=== AST ===\n");
    printAst(root, 0);

What this parser currently supports cleanly:
- expression precedence
- assignment
- function definitions
- class definitions
- if / elif / else
- while
- return / pass / break / continue
- member access: a.b
- calls: f(x, y)

Good next upgrades once this is stable:
1. for-in loops
2. list literals
3. indexing: a[0]
4. unary/binary power ops if wanted
5. argument defaults in def
6. better AST memory cleanup
7. split into parser.h / parser.c / ast.h
*/
