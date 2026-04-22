#include <stdio.h>
#include "parser.h"

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
    if (parser->current <= 0) {
        return NULL;
    }
    return &parser->tokens->data[parser->current - 1];
}

static int parserIsAtEnd(ParseState* parser) {
    return parserPeek(parser)->type == TOKEN_EOF;
}

static Token* parserAdvance(ParseState* parser) {
    if (!parserIsAtEnd(parser)) {
        parser->current++;
    }
    return parserPrevious(parser);
}

static int parserCheck(ParseState* parser, TokenType type) {
    if (parserIsAtEnd(parser)) {
        return type == TOKEN_EOF;
    }
    return parserPeek(parser)->type == type;
}

static int parserMatch(ParseState* parser, TokenType type) {
    if (!parserCheck(parser, type)) {
        return 0;
    }
    parserAdvance(parser);
    return 1;
}

static void parserErrorAtToken(ParseState* parser, Token* token, const char* message) {
    parser->hadError = 1;

    if (parser->panicMode) {
        return;
    }

    parser->panicMode = 1;

    fprintf(stderr,
            "[parse error] line %d col %d near '%.*s': %s\n",
            token->line,
            token->column,
            token->length,
            token->start,
            message);
}

static Token* parserConsume(ParseState* parser, TokenType type, const char* message) {
    if (parserCheck(parser, type)) {
        return parserAdvance(parser);
    }

    parserErrorAtToken(parser, parserPeek(parser), message);
    return NULL;
}

static void parserSynchronize(ParseState* parser) {
    parser->panicMode = 0;

    if (!parserIsAtEnd(parser)) {
        parserAdvance(parser);
    }

    while (!parserIsAtEnd(parser)) {
        Token* prev = parserPrevious(parser);

        if (prev != NULL && prev->type == TOKEN_NEWLINE) {
            return;
        }

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

static int consumeStatementTerminator(ParseState* parser, const char* message) {
    if (parserMatch(parser, TOKEN_NEWLINE) ||
        parserCheck(parser, TOKEN_DEDENT) ||
        parserCheck(parser, TOKEN_EOF)) {
        return 1;
    }

    parserErrorAtToken(parser, parserPeek(parser), message);
    return 0;
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
static AstNode* parseListLiteral(ParseState* parser);

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

    if (defTok == NULL || name == NULL) {
        return NULL;
    }

    node = newAstNode(AST_FUNCTION_DEF, *defTok);
    node->as.functionDef.name = *name;
    initTokenList(&node->as.functionDef.parameters);

    if (parserConsume(parser, TOKEN_LPAREN, "Expected '(' after function name.") == NULL) {
        return NULL;
    }

    if (!parserCheck(parser, TOKEN_RPAREN)) {
        do {
            Token* param = parserConsume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
            if (param == NULL) {
                return NULL;
            }
            pushTokenList(&node->as.functionDef.parameters, *param);
        } while (parserMatch(parser, TOKEN_COMMA));
    }

    if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after parameter list.") == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after function signature.") == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after function signature.") == NULL) {
        return NULL;
    }

    node->as.functionDef.body = parseBlock(parser);
    return node;
}

static AstNode* parseClassDef(ParseState* parser) {
    Token* classTok = parserConsume(parser, TOKEN_CLASS, "Expected 'class'.");
    Token* name = parserConsume(parser, TOKEN_IDENTIFIER, "Expected class name.");
    AstNode* node;

    if (classTok == NULL || name == NULL) {
        return NULL;
    }

    node = newAstNode(AST_CLASS_DEF, *classTok);
    node->as.classDef.name = *name;

    if (parserMatch(parser, TOKEN_LPAREN)) {
        if (!parserCheck(parser, TOKEN_RPAREN)) {
            if (parseExpression(parser) == NULL) {
                return NULL;
            }

            while (parserMatch(parser, TOKEN_COMMA)) {
                if (parseExpression(parser) == NULL) {
                    return NULL;
                }
            }
        }

        if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after base list.") == NULL) {
            return NULL;
        }
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after class name.") == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after class header.") == NULL) {
        return NULL;
    }

    node->as.classDef.body = parseBlock(parser);
    return node;
}

static AstNode* parseDeclaration(ParseState* parser) {
    if (parserCheck(parser, TOKEN_DEF)) {
        return parseFunctionDef(parser);
    }

    if (parserCheck(parser, TOKEN_CLASS)) {
        return parseClassDef(parser);
    }

    return parseStatement(parser);
}

// ------------------------------------------------------------
// BLOCKS / STATEMENTS
// ------------------------------------------------------------

static AstNode* parseBlock(ParseState* parser) {
    AstNode* block;

    if (parserConsume(parser, TOKEN_INDENT, "Expected indented block.") == NULL) {
        return NULL;
    }

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

    if (parserConsume(parser, TOKEN_DEDENT, "Expected end of indented block.") == NULL) {
        return NULL;
    }

    return block;
}

static AstNode* parseReturnStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_RETURN, "Expected 'return'.");
    AstNode* node;

    if (tok == NULL) {
        return NULL;
    }

    node = newAstNode(AST_RETURN_STMT, *tok);

    if (parserCheck(parser, TOKEN_NEWLINE) ||
        parserCheck(parser, TOKEN_DEDENT) ||
        parserCheck(parser, TOKEN_EOF)) {
        node->as.returnStmt.value = NULL;
    } else {
        node->as.returnStmt.value = parseExpression(parser);
        if (node->as.returnStmt.value == NULL) {
            return NULL;
        }
    }

    if (!consumeStatementTerminator(parser, "Expected newline after return statement.")) {
        return NULL;
    }

    return node;
}

static AstNode* parsePassStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_PASS, "Expected 'pass'.");
    AstNode* node;

    if (tok == NULL) {
        return NULL;
    }

    node = newAstNode(AST_PASS_STMT, *tok);

    if (!consumeStatementTerminator(parser, "Expected newline after pass.")) {
        return NULL;
    }

    return node;
}

static AstNode* parseBreakStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_BREAK, "Expected 'break'.");
    AstNode* node;

    if (tok == NULL) {
        return NULL;
    }

    node = newAstNode(AST_BREAK_STMT, *tok);

    if (!consumeStatementTerminator(parser, "Expected newline after break.")) {
        return NULL;
    }

    return node;
}

static AstNode* parseContinueStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_CONTINUE, "Expected 'continue'.");
    AstNode* node;

    if (tok == NULL) {
        return NULL;
    }

    node = newAstNode(AST_CONTINUE_STMT, *tok);

    if (!consumeStatementTerminator(parser, "Expected newline after continue.")) {
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

    if (tok == NULL) {
        return NULL;
    }

    node = newAstNode(AST_IF_STMT, *tok);
    node->as.ifStmt.condition = parseExpression(parser);

    if (node->as.ifStmt.condition == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after if condition.") == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after if header.") == NULL) {
        return NULL;
    }

    node->as.ifStmt.thenBlock = parseBlock(parser);
    if (node->as.ifStmt.thenBlock == NULL) {
        return NULL;
    }

    node->as.ifStmt.elseBranch = NULL;

    skipNewlines(parser);

    if (parserCheck(parser, TOKEN_ELIF)) {
        node->as.ifStmt.elseBranch = parseIfLikeStatement(parser, 1);
        return node;
    }

    if (parserMatch(parser, TOKEN_ELSE)) {
        if (parserConsume(parser, TOKEN_COLON, "Expected ':' after else.") == NULL) {
            return NULL;
        }

        if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after else header.") == NULL) {
            return NULL;
        }

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

    if (tok == NULL) {
        return NULL;
    }

    node = newAstNode(AST_WHILE_STMT, *tok);
    node->as.whileStmt.condition = parseExpression(parser);

    if (node->as.whileStmt.condition == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after while condition.") == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_NEWLINE, "Expected newline after while header.") == NULL) {
        return NULL;
    }

    node->as.whileStmt.body = parseBlock(parser);
    return node;
}

static AstNode* parseForStatement(ParseState* parser) {
    Token* tok = parserConsume(parser, TOKEN_FOR, "Expected 'for'.");
    Token* name;
    AstNode* node;
    AstNode* target;
    AstNode* iterable;

    if (tok == NULL) {
        return NULL;
    }

    name = parserConsume(parser, TOKEN_IDENTIFIER, "Expected loop variable after 'for'.");
    if (name == NULL) {
        return NULL;
    }

    if (parserConsume(parser, TOKEN_IN, "Expected 'in' after loop variable.") == NULL) {
        return NULL;
    }

    iterable = parseExpression(parser);
    if (iterable == NULL) {
        return NULL;
    }

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

static AstNode* parseExpressionStatement(ParseState* parser) {
    AstNode* expr = parseExpression(parser);
    AstNode* stmt;

    if (expr == NULL) {
        return NULL;
    }

    if (!consumeStatementTerminator(parser, "Expected newline after statement.")) {
        return NULL;
    }

    if (expr->type == AST_ASSIGN_STMT) {
        return expr;
    }

    stmt = newAstNode(AST_EXPR_STMT, expr->token);
    stmt->as.exprStmt.expression = expr;
    return stmt;
}

static AstNode* parseStatement(ParseState* parser) {
    if (parserCheck(parser, TOKEN_IF)) {
        return parseIfStatement(parser);
    }

    if (parserCheck(parser, TOKEN_WHILE)) {
        return parseWhileStatement(parser);
    }

    if (parserCheck(parser, TOKEN_RETURN)) {
        return parseReturnStatement(parser);
    }

    if (parserCheck(parser, TOKEN_PASS)) {
        return parsePassStatement(parser);
    }

    if (parserCheck(parser, TOKEN_BREAK)) {
        return parseBreakStatement(parser);
    }

    if (parserCheck(parser, TOKEN_CONTINUE)) {
        return parseContinueStatement(parser);
    }

    if (parserCheck(parser, TOKEN_FOR)) {
        return parseForStatement(parser);
    }

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

    if (left == NULL) {
        return NULL;
    }

    if (parserMatch(parser, TOKEN_EQUAL)) {
        Token op = *parserPrevious(parser);
        AstNode* value = parseAssignment(parser);
        AstNode* node;

        if (value == NULL) {
            return NULL;
        }

        if (left->type != AST_IDENTIFIER_EXPR &&
            left->type != AST_MEMBER_EXPR) {
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

        if (right == NULL) {
            return NULL;
        }

        expr = makeBinaryNode(op, expr, right);
    }

    return expr;
}

static AstNode* parseAnd(ParseState* parser) {
    AstNode* expr = parseEquality(parser);

    while (parserMatch(parser, TOKEN_AND)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseEquality(parser);

        if (right == NULL) {
            return NULL;
        }

        expr = makeBinaryNode(op, expr, right);
    }

    return expr;
}

static AstNode* parseEquality(ParseState* parser) {
    AstNode* expr = parseComparison(parser);

    while (parserMatch(parser, TOKEN_EQUAL_EQUAL) ||
           parserMatch(parser, TOKEN_NOT_EQUAL)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseComparison(parser);

        if (right == NULL) {
            return NULL;
        }

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

        if (right == NULL) {
            return NULL;
        }

        expr = makeBinaryNode(op, expr, right);
    }

    return expr;
}

static AstNode* parseTerm(ParseState* parser) {
    AstNode* expr = parseFactor(parser);

    while (parserMatch(parser, TOKEN_PLUS) ||
           parserMatch(parser, TOKEN_MINUS)) {
        Token op = *parserPrevious(parser);
        AstNode* right = parseFactor(parser);

        if (right == NULL) {
            return NULL;
        }

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

        if (right == NULL) {
            return NULL;
        }

        expr = makeBinaryNode(op, expr, right);
    }

    return expr;
}

static AstNode* parseUnary(ParseState* parser) {
    if (parserMatch(parser, TOKEN_NOT) ||
        parserMatch(parser, TOKEN_MINUS) ||
        parserMatch(parser, TOKEN_PLUS)) {
        Token op = *parserPrevious(parser);
        AstNode* operand = parseUnary(parser);

        if (operand == NULL) {
            return NULL;
        }

        return makeUnaryNode(op, operand);
    }

    return parseCall(parser);
}

static AstNode* parseCall(ParseState* parser) {
    AstNode* expr = parsePrimary(parser);

    if (expr == NULL) {
        return NULL;
    }

    for (;;) {
        if (parserMatch(parser, TOKEN_LPAREN)) {
            AstNode* call = newAstNode(AST_CALL_EXPR, *parserPrevious(parser));
            initAstNodeArray(&call->as.callExpr.arguments);
            call->as.callExpr.callee = expr;

            if (!parserCheck(parser, TOKEN_RPAREN)) {
                do {
                    AstNode* arg = parseExpression(parser);

                    if (arg == NULL) {
                        return NULL;
                    }

                    pushAstNode(&call->as.callExpr.arguments, arg);
                } while (parserMatch(parser, TOKEN_COMMA));
            }

            if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after arguments.") == NULL) {
                return NULL;
            }

            expr = call;
            continue;
        }

        if (parserMatch(parser, TOKEN_DOT)) {
            Token* member = parserConsume(parser, TOKEN_IDENTIFIER, "Expected member name after '.'.");
            AstNode* access;

            if (member == NULL) {
                return NULL;
            }

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

static AstNode* parseListLiteral(ParseState* parser) {
    Token open;
    AstNode* list;

    if (!parserMatch(parser, TOKEN_LBRACKET)) {
        parserErrorAtToken(parser, parserPeek(parser), "Expected '['.");
        return NULL;
    }

    open = *parserPrevious(parser);
    list = newAstNode(AST_LIST_EXPR, open);
    initAstNodeArray(&list->as.listExpr.elements);

    if (!parserCheck(parser, TOKEN_RBRACKET)) {
        do {
            AstNode* element = parseExpression(parser);
            if (element == NULL) {
                return NULL;
            }

            pushAstNode(&list->as.listExpr.elements, element);
        } while (parserMatch(parser, TOKEN_COMMA));
    }

    if (parserConsume(parser, TOKEN_RBRACKET, "Expected ']' after list literal.") == NULL) {
        return NULL;
    }

    return list;
}

static AstNode* parsePrimary(ParseState* parser) {
    if (parserMatch(parser, TOKEN_NUMBER) ||
        parserMatch(parser, TOKEN_STRING) ||
        parserMatch(parser, TOKEN_TRUE) ||
        parserMatch(parser, TOKEN_FALSE) ||
        parserMatch(parser, TOKEN_NONE)) {
        return makeLiteralNode(*parserPrevious(parser));
    }

    if (parserMatch(parser, TOKEN_NUMBER) ||
    parserMatch(parser, TOKEN_STRING) ||
    parserMatch(parser, TOKEN_TRUE) ||
    parserMatch(parser, TOKEN_FALSE) ||
    parserMatch(parser, TOKEN_NONE)) {
        return makeLiteralNode(*parserPrevious(parser));
    }

    if (parserCheck(parser, TOKEN_LBRACKET)) {
        return parseListLiteral(parser);
    }

    if (parserMatch(parser, TOKEN_IDENTIFIER)) {
        return makeIdentifierNode(*parserPrevious(parser));
    }

    if (parserMatch(parser, TOKEN_LPAREN)) {
        Token open = *parserPrevious(parser);
        AstNode* expr = parseExpression(parser);
        AstNode* grouping;

        if (expr == NULL) {
            return NULL;
        }

        if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after expression.") == NULL) {
            return NULL;
        }

        grouping = newAstNode(AST_GROUPING_EXPR, open);
        grouping->as.groupingExpr.expression = expr;
        return grouping;
    }

    parserErrorAtToken(parser, parserPeek(parser), "Expected expression.");
    return NULL;
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