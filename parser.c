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
// FORWARD DECLARATIONS
// ------------------------------------------------------------

static void initParseState(ParseState* parser, TokenArray* tokens);

static Token* parserPeek(ParseState* parser);
static Token* parserPrevious(ParseState* parser);
static int parserIsAtEnd(ParseState* parser);
static Token* parserAdvance(ParseState* parser);
static int parserCheck(ParseState* parser, TokenType type);
static int parserMatch(ParseState* parser, TokenType type);
static Token* parserConsume(ParseState* parser, TokenType type, const char* message);

static void parserErrorAtToken(ParseState* parser, Token* token, const char* message);
static void parserSynchronize(ParseState* parser);
static void skipNewlines(ParseState* parser);
static int consumeStatementTerminator(ParseState* parser, const char* message);

static AstNode* parseModule(ParseState* parser);
static AstNode* parseDeclaration(ParseState* parser);

static AstNode* parseFunctionDef(ParseState* parser);
static AstNode* parseClassDef(ParseState* parser);

static AstNode* parseBlock(ParseState* parser);
static AstNode* parseStatement(ParseState* parser);
static AstNode* parseReturnStatement(ParseState* parser);
static AstNode* parsePassStatement(ParseState* parser);
static AstNode* parseBreakStatement(ParseState* parser);
static AstNode* parseContinueStatement(ParseState* parser);
static AstNode* parseIfStatement(ParseState* parser);
static AstNode* parseIfLikeStatement(ParseState* parser, int startedWithElif);
static AstNode* parseWhileStatement(ParseState* parser);
static AstNode* parseForStatement(ParseState* parser);
static AstNode* parseExpressionStatement(ParseState* parser);

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
static AstNode* parseListLiteral(ParseState* parser);
static AstNode* parsePrimary(ParseState* parser);

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

static Token* parserConsume(ParseState* parser, TokenType type, const char* message) {
    if (parserCheck(parser, type)) {
        return parserAdvance(parser);
    }

    parserErrorAtToken(parser, parserPeek(parser), message);
    return NULL;
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

static void parserSynchronize(ParseState* parser) {
    parser->panicMode = 0;

    if (!parserIsAtEnd(parser)) {
        parserAdvance(parser);
    }

    while (!parserIsAtEnd(parser)) {
        Token* previous = parserPrevious(parser);

        if (previous != NULL && previous->type == TOKEN_NEWLINE) {
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
// PROGRAM / DECLARATIONS
// ------------------------------------------------------------

static AstNode* parseModule(ParseState* parser) {
    AstNode* module = newAstNode(AST_MODULE, *parserPeek(parser));

    initAstNodeArray(&module->as.module.statements);
    skipNewlines(parser);

    while (!parserIsAtEnd(parser)) {
        AstNode* statement = parseDeclaration(parser);

        if (statement != NULL) {
            pushAstNode(&module->as.module.statements, statement);
        } else {
            parserSynchronize(parser);
        }

        skipNewlines(parser);
    }

    return module;
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

static AstNode* parseFunctionDef(ParseState* parser) {
    Token* defToken = parserConsume(parser, TOKEN_DEF, "Expected 'def'.");
    Token* nameToken = parserConsume(parser, TOKEN_IDENTIFIER, "Expected function name.");
    AstNode* node;

    if (defToken == NULL || nameToken == NULL) {
        return NULL;
    }

    node = newAstNode(AST_FUNCTION_DEF, *defToken);
    node->as.functionDef.name = *nameToken;
    initTokenList(&node->as.functionDef.parameters);

    if (parserConsume(parser, TOKEN_LPAREN, "Expected '(' after function name.") == NULL) {
        freeAst(node);
        return NULL;
    }

    if (!parserCheck(parser, TOKEN_RPAREN)) {
        do {
            Token* parameter = parserConsume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");

            if (parameter == NULL) {
                freeAst(node);
                return NULL;
            }

            pushTokenList(&node->as.functionDef.parameters, *parameter);
        } while (parserMatch(parser, TOKEN_COMMA));
    }

    if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after parameter list.") == NULL ||
        parserConsume(parser, TOKEN_COLON, "Expected ':' after function signature.") == NULL ||
        parserConsume(parser, TOKEN_NEWLINE, "Expected newline after function signature.") == NULL) {
        freeAst(node);
        return NULL;
    }

    node->as.functionDef.body = parseBlock(parser);

    if (node->as.functionDef.body == NULL) {
        freeAst(node);
        return NULL;
    }

    return node;
}

static AstNode* parseClassDef(ParseState* parser) {
    Token* classToken = parserConsume(parser, TOKEN_CLASS, "Expected 'class'.");
    Token* nameToken = parserConsume(parser, TOKEN_IDENTIFIER, "Expected class name.");
    AstNode* node;

    if (classToken == NULL || nameToken == NULL) {
        return NULL;
    }

    node = newAstNode(AST_CLASS_DEF, *classToken);
    node->as.classDef.name = *nameToken;

    if (parserMatch(parser, TOKEN_LPAREN)) {
        if (!parserCheck(parser, TOKEN_RPAREN)) {
            AstNode* base = parseExpression(parser);

            if (base == NULL) {
                freeAst(node);
                return NULL;
            }

            freeAst(base);

            while (parserMatch(parser, TOKEN_COMMA)) {
                base = parseExpression(parser);

                if (base == NULL) {
                    freeAst(node);
                    return NULL;
                }

                freeAst(base);
            }
        }

        if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after base list.") == NULL) {
            freeAst(node);
            return NULL;
        }
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after class name.") == NULL ||
        parserConsume(parser, TOKEN_NEWLINE, "Expected newline after class header.") == NULL) {
        freeAst(node);
        return NULL;
    }

    node->as.classDef.body = parseBlock(parser);

    if (node->as.classDef.body == NULL) {
        freeAst(node);
        return NULL;
    }

    return node;
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
        AstNode* statement = parseDeclaration(parser);

        if (statement != NULL) {
            pushAstNode(&block->as.block.statements, statement);
        } else {
            parserSynchronize(parser);
        }

        skipNewlines(parser);
    }

    if (parserConsume(parser, TOKEN_DEDENT, "Expected end of indented block.") == NULL) {
        freeAst(block);
        return NULL;
    }

    return block;
}

static AstNode* parseStatement(ParseState* parser) {
    if (parserCheck(parser, TOKEN_IF)) {
        return parseIfStatement(parser);
    }

    if (parserCheck(parser, TOKEN_WHILE)) {
        return parseWhileStatement(parser);
    }

    if (parserCheck(parser, TOKEN_FOR)) {
        return parseForStatement(parser);
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

    return parseExpressionStatement(parser);
}

static AstNode* parseReturnStatement(ParseState* parser) {
    Token* token = parserConsume(parser, TOKEN_RETURN, "Expected 'return'.");
    AstNode* node;

    if (token == NULL) {
        return NULL;
    }

    node = newAstNode(AST_RETURN_STMT, *token);

    if (parserCheck(parser, TOKEN_NEWLINE) ||
        parserCheck(parser, TOKEN_DEDENT) ||
        parserCheck(parser, TOKEN_EOF)) {
        node->as.returnStmt.value = NULL;
    } else {
        node->as.returnStmt.value = parseExpression(parser);

        if (node->as.returnStmt.value == NULL) {
            freeAst(node);
            return NULL;
        }
    }

    if (!consumeStatementTerminator(parser, "Expected newline after return statement.")) {
        freeAst(node);
        return NULL;
    }

    return node;
}

static AstNode* parsePassStatement(ParseState* parser) {
    Token* token = parserConsume(parser, TOKEN_PASS, "Expected 'pass'.");
    AstNode* node;

    if (token == NULL) {
        return NULL;
    }

    node = newAstNode(AST_PASS_STMT, *token);

    if (!consumeStatementTerminator(parser, "Expected newline after pass.")) {
        freeAst(node);
        return NULL;
    }

    return node;
}

static AstNode* parseBreakStatement(ParseState* parser) {
    Token* token = parserConsume(parser, TOKEN_BREAK, "Expected 'break'.");
    AstNode* node;

    if (token == NULL) {
        return NULL;
    }

    node = newAstNode(AST_BREAK_STMT, *token);

    if (!consumeStatementTerminator(parser, "Expected newline after break.")) {
        freeAst(node);
        return NULL;
    }

    return node;
}

static AstNode* parseContinueStatement(ParseState* parser) {
    Token* token = parserConsume(parser, TOKEN_CONTINUE, "Expected 'continue'.");
    AstNode* node;

    if (token == NULL) {
        return NULL;
    }

    node = newAstNode(AST_CONTINUE_STMT, *token);

    if (!consumeStatementTerminator(parser, "Expected newline after continue.")) {
        freeAst(node);
        return NULL;
    }

    return node;
}

static AstNode* parseIfStatement(ParseState* parser) {
    return parseIfLikeStatement(parser, 0);
}

static AstNode* parseIfLikeStatement(ParseState* parser, int startedWithElif) {
    Token* token;
    AstNode* node;

    if (startedWithElif) {
        token = parserConsume(parser, TOKEN_ELIF, "Expected 'elif'.");
    } else {
        token = parserConsume(parser, TOKEN_IF, "Expected 'if'.");
    }

    if (token == NULL) {
        return NULL;
    }

    node = newAstNode(AST_IF_STMT, *token);
    node->as.ifStmt.condition = parseExpression(parser);

    if (node->as.ifStmt.condition == NULL) {
        freeAst(node);
        return NULL;
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after if condition.") == NULL ||
        parserConsume(parser, TOKEN_NEWLINE, "Expected newline after if header.") == NULL) {
        freeAst(node);
        return NULL;
    }

    node->as.ifStmt.thenBlock = parseBlock(parser);

    if (node->as.ifStmt.thenBlock == NULL) {
        freeAst(node);
        return NULL;
    }

    node->as.ifStmt.elseBranch = NULL;
    skipNewlines(parser);

    if (parserCheck(parser, TOKEN_ELIF)) {
        node->as.ifStmt.elseBranch = parseIfLikeStatement(parser, 1);

        if (node->as.ifStmt.elseBranch == NULL) {
            freeAst(node);
            return NULL;
        }

        return node;
    }

    if (parserMatch(parser, TOKEN_ELSE)) {
        if (parserConsume(parser, TOKEN_COLON, "Expected ':' after else.") == NULL ||
            parserConsume(parser, TOKEN_NEWLINE, "Expected newline after else header.") == NULL) {
            freeAst(node);
            return NULL;
        }

        node->as.ifStmt.elseBranch = parseBlock(parser);

        if (node->as.ifStmt.elseBranch == NULL) {
            freeAst(node);
            return NULL;
        }
    }

    return node;
}

static AstNode* parseWhileStatement(ParseState* parser) {
    Token* token = parserConsume(parser, TOKEN_WHILE, "Expected 'while'.");
    AstNode* node;

    if (token == NULL) {
        return NULL;
    }

    node = newAstNode(AST_WHILE_STMT, *token);
    node->as.whileStmt.condition = parseExpression(parser);

    if (node->as.whileStmt.condition == NULL) {
        freeAst(node);
        return NULL;
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after while condition.") == NULL ||
        parserConsume(parser, TOKEN_NEWLINE, "Expected newline after while header.") == NULL) {
        freeAst(node);
        return NULL;
    }

    node->as.whileStmt.body = parseBlock(parser);

    if (node->as.whileStmt.body == NULL) {
        freeAst(node);
        return NULL;
    }

    return node;
}

static AstNode* parseForStatement(ParseState* parser) {
    Token* token = parserConsume(parser, TOKEN_FOR, "Expected 'for'.");
    Token* nameToken;
    AstNode* node;

    if (token == NULL) {
        return NULL;
    }

    nameToken = parserConsume(parser, TOKEN_IDENTIFIER, "Expected loop variable after 'for'.");

    if (nameToken == NULL) {
        return NULL;
    }

    node = newAstNode(AST_FOR_STMT, *token);
    node->as.forStmt.target = makeIdentifierNode(*nameToken);

    if (parserConsume(parser, TOKEN_IN, "Expected 'in' after loop variable.") == NULL) {
        freeAst(node);
        return NULL;
    }

    node->as.forStmt.iterable = parseExpression(parser);

    if (node->as.forStmt.iterable == NULL) {
        freeAst(node);
        return NULL;
    }

    if (parserConsume(parser, TOKEN_COLON, "Expected ':' after for loop header.") == NULL ||
        parserConsume(parser, TOKEN_NEWLINE, "Expected newline after for loop header.") == NULL) {
        freeAst(node);
        return NULL;
    }

    node->as.forStmt.body = parseBlock(parser);

    if (node->as.forStmt.body == NULL) {
        freeAst(node);
        return NULL;
    }

    return node;
}

static AstNode* parseExpressionStatement(ParseState* parser) {
    AstNode* expression = parseExpression(parser);
    AstNode* statement;

    if (expression == NULL) {
        return NULL;
    }

    if (!consumeStatementTerminator(parser, "Expected newline after statement.")) {
        freeAst(expression);
        return NULL;
    }

    if (expression->type == AST_ASSIGN_STMT) {
        return expression;
    }

    statement = newAstNode(AST_EXPR_STMT, expression->token);
    statement->as.exprStmt.expression = expression;

    return statement;
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
        Token operatorToken = *parserPrevious(parser);
        AstNode* value = parseAssignment(parser);
        AstNode* node;

        if (value == NULL) {
            freeAst(left);
            return NULL;
        }

        if (left->type != AST_IDENTIFIER_EXPR &&
            left->type != AST_MEMBER_EXPR &&
            left->type != AST_INDEX_EXPR) {
            parserErrorAtToken(parser, &operatorToken, "Invalid assignment target.");
            freeAst(left);
            freeAst(value);
            return NULL;
        }

        node = newAstNode(AST_ASSIGN_STMT, operatorToken);
        node->as.assignStmt.target = left;
        node->as.assignStmt.value = value;

        return node;
    }

    return left;
}

static AstNode* parseOr(ParseState* parser) {
    AstNode* expression = parseAnd(parser);

    while (parserMatch(parser, TOKEN_OR)) {
        Token operatorToken = *parserPrevious(parser);
        AstNode* right = parseAnd(parser);

        if (right == NULL) {
            freeAst(expression);
            return NULL;
        }

        expression = makeBinaryNode(operatorToken, expression, right);
    }

    return expression;
}

static AstNode* parseAnd(ParseState* parser) {
    AstNode* expression = parseEquality(parser);

    while (parserMatch(parser, TOKEN_AND)) {
        Token operatorToken = *parserPrevious(parser);
        AstNode* right = parseEquality(parser);

        if (right == NULL) {
            freeAst(expression);
            return NULL;
        }

        expression = makeBinaryNode(operatorToken, expression, right);
    }

    return expression;
}

static AstNode* parseEquality(ParseState* parser) {
    AstNode* expression = parseComparison(parser);

    while (parserMatch(parser, TOKEN_EQUAL_EQUAL) ||
           parserMatch(parser, TOKEN_NOT_EQUAL)) {
        Token operatorToken = *parserPrevious(parser);
        AstNode* right = parseComparison(parser);

        if (right == NULL) {
            freeAst(expression);
            return NULL;
        }

        expression = makeBinaryNode(operatorToken, expression, right);
    }

    return expression;
}

static AstNode* parseComparison(ParseState* parser) {
    AstNode* expression = parseTerm(parser);

    while (parserMatch(parser, TOKEN_LESS) ||
           parserMatch(parser, TOKEN_LESS_EQUAL) ||
           parserMatch(parser, TOKEN_GREATER) ||
           parserMatch(parser, TOKEN_GREATER_EQUAL)) {
        Token operatorToken = *parserPrevious(parser);
        AstNode* right = parseTerm(parser);

        if (right == NULL) {
            freeAst(expression);
            return NULL;
        }

        expression = makeBinaryNode(operatorToken, expression, right);
    }

    return expression;
}

static AstNode* parseTerm(ParseState* parser) {
    AstNode* expression = parseFactor(parser);

    while (parserMatch(parser, TOKEN_PLUS) ||
           parserMatch(parser, TOKEN_MINUS)) {
        Token operatorToken = *parserPrevious(parser);
        AstNode* right = parseFactor(parser);

        if (right == NULL) {
            freeAst(expression);
            return NULL;
        }

        expression = makeBinaryNode(operatorToken, expression, right);
    }

    return expression;
}

static AstNode* parseFactor(ParseState* parser) {
    AstNode* expression = parseUnary(parser);

    while (parserMatch(parser, TOKEN_STAR) ||
           parserMatch(parser, TOKEN_SLASH) ||
           parserMatch(parser, TOKEN_PERCENT)) {
        Token operatorToken = *parserPrevious(parser);
        AstNode* right = parseUnary(parser);

        if (right == NULL) {
            freeAst(expression);
            return NULL;
        }

        expression = makeBinaryNode(operatorToken, expression, right);
    }

    return expression;
}

static AstNode* parseUnary(ParseState* parser) {
    if (parserMatch(parser, TOKEN_NOT) ||
        parserMatch(parser, TOKEN_MINUS) ||
        parserMatch(parser, TOKEN_PLUS)) {
        Token operatorToken = *parserPrevious(parser);
        AstNode* operand = parseUnary(parser);

        if (operand == NULL) {
            return NULL;
        }

        return makeUnaryNode(operatorToken, operand);
    }

    return parseCall(parser);
}

static AstNode* parseCall(ParseState* parser) {
    AstNode* expression = parsePrimary(parser);

    if (expression == NULL) {
        return NULL;
    }

    for (;;) {
        if (parserMatch(parser, TOKEN_LPAREN)) {
            AstNode* call = newAstNode(AST_CALL_EXPR, *parserPrevious(parser));
            initAstNodeArray(&call->as.callExpr.arguments);
            call->as.callExpr.callee = expression;

            if (!parserCheck(parser, TOKEN_RPAREN)) {
                do {
                    AstNode* argument = parseExpression(parser);

                    if (argument == NULL) {
                        freeAst(call);
                        return NULL;
                    }

                    pushAstNode(&call->as.callExpr.arguments, argument);
                } while (parserMatch(parser, TOKEN_COMMA));
            }

            if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after arguments.") == NULL) {
                freeAst(call);
                return NULL;
            }

            expression = call;
            continue;
        }

        if (parserMatch(parser, TOKEN_DOT)) {
            Token* member = parserConsume(parser, TOKEN_IDENTIFIER, "Expected member name after '.'.");

            if (member == NULL) {
                freeAst(expression);
                return NULL;
            }

            AstNode* access = newAstNode(AST_MEMBER_EXPR, *member);
            access->as.memberExpr.object = expression;
            access->as.memberExpr.member = *member;

            expression = access;
            continue;
        }

        if (parserMatch(parser, TOKEN_LBRACKET)) {
            Token openToken = *parserPrevious(parser);
            AstNode* index = parseExpression(parser);

            if (index == NULL) {
                freeAst(expression);
                return NULL;
            }

            if (parserConsume(parser, TOKEN_RBRACKET, "Expected ']' after index.") == NULL) {
                freeAst(expression);
                freeAst(index);
                return NULL;
            }

            AstNode* access = newAstNode(AST_INDEX_EXPR, openToken);
            access->as.indexExpr.object = expression;
            access->as.indexExpr.index = index;

            expression = access;
            continue;
        }

        break;
    }

    return expression;
}

static AstNode* parseListLiteral(ParseState* parser) {
    Token openToken;
    AstNode* list;

    if (!parserMatch(parser, TOKEN_LBRACKET)) {
        parserErrorAtToken(parser, parserPeek(parser), "Expected '['.");
        return NULL;
    }

    openToken = *parserPrevious(parser);
    list = newAstNode(AST_LIST_EXPR, openToken);
    initAstNodeArray(&list->as.listExpr.elements);

    if (!parserCheck(parser, TOKEN_RBRACKET)) {
        do {
            AstNode* element = parseExpression(parser);

            if (element == NULL) {
                freeAst(list);
                return NULL;
            }

            pushAstNode(&list->as.listExpr.elements, element);
        } while (parserMatch(parser, TOKEN_COMMA));
    }

    if (parserConsume(parser, TOKEN_RBRACKET, "Expected ']' after list literal.") == NULL) {
        freeAst(list);
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

    if (parserCheck(parser, TOKEN_LBRACKET)) {
        return parseListLiteral(parser);
    }

    if (parserMatch(parser, TOKEN_IDENTIFIER)) {
        return makeIdentifierNode(*parserPrevious(parser));
    }

    if (parserMatch(parser, TOKEN_LPAREN)) {
        Token openToken = *parserPrevious(parser);
        AstNode* expression = parseExpression(parser);
        AstNode* grouping;

        if (expression == NULL) {
            return NULL;
        }

        if (parserConsume(parser, TOKEN_RPAREN, "Expected ')' after expression.") == NULL) {
            freeAst(expression);
            return NULL;
        }

        grouping = newAstNode(AST_GROUPING_EXPR, openToken);
        grouping->as.groupingExpr.expression = expression;

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
        freeAst(root);
        return NULL;
    }

    return root;
}