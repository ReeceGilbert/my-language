// lexer.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"

typedef struct {
    const char* source;
    const char* start;
    const char* current;

    int line;
    int column;
    int offset;

    int tokenLine;
    int tokenColumn;
    int tokenOffset;

    int indentStack[MAX_INDENTS];
    int indentCount;
    int pendingDedents;

    int atLineStart;
    int groupDepth;

    Diagnostics* diagnostics;
} Scanner;

void initDiagnostics(Diagnostics* diagnostics) {
    diagnostics->errorCount = 0;
    diagnostics->warningCount = 0;
}

static void reportLexerError(
    Diagnostics* diagnostics,
    int line,
    int column,
    int offset,
    const char* message
) {
    diagnostics->errorCount++;
    fprintf(stderr,
            "[lex error] line %d, col %d, off %d: %s\n",
            line, column, offset, message);
}

void initTokenArray(TokenArray* array) {
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
}

void freeTokenArray(TokenArray* array) {
    free(array->data);
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
}

static void pushToken(TokenArray* array, Token token) {
    if (array->count >= array->capacity) {
        int newCapacity = (array->capacity < 8) ? 8 : array->capacity * 2;
        Token* newData = (Token*)realloc(array->data, sizeof(Token) * newCapacity);

        if (newData == NULL) {
            fprintf(stderr, "Out of memory while growing token array.\n");
            exit(1);
        }

        array->data = newData;
        array->capacity = newCapacity;
    }

    array->data[array->count++] = token;
}

static void initScanner(Scanner* scanner, const char* source, Diagnostics* diagnostics) {
    scanner->source = source;
    scanner->start = source;
    scanner->current = source;

    scanner->line = 1;
    scanner->column = 1;
    scanner->offset = 0;

    scanner->tokenLine = 1;
    scanner->tokenColumn = 1;
    scanner->tokenOffset = 0;

    scanner->indentStack[0] = 0;
    scanner->indentCount = 1;
    scanner->pendingDedents = 0;

    scanner->atLineStart = 1;
    scanner->groupDepth = 0;

    scanner->diagnostics = diagnostics;
}

static int isAtEnd(Scanner* scanner) {
    return *scanner->current == '\0';
}

static char peekChar(Scanner* scanner) {
    return *scanner->current;
}

static char peekNextChar(Scanner* scanner) {
    if (isAtEnd(scanner) || scanner->current[1] == '\0') {
        return '\0';
    }
    return scanner->current[1];
}

static char advanceChar(Scanner* scanner) {
    char c = *scanner->current;
    scanner->current++;
    scanner->offset++;

    if (c == '\n') {
        scanner->line++;
        scanner->column = 1;
        scanner->atLineStart = 1;
    } else {
        scanner->column++;
    }

    return c;
}

static int matchChar(Scanner* scanner, char expected) {
    if (isAtEnd(scanner)) return 0;
    if (*scanner->current != expected) return 0;
    advanceChar(scanner);
    return 1;
}

static void markTokenStart(Scanner* scanner) {
    scanner->start = scanner->current;
    scanner->tokenLine = scanner->line;
    scanner->tokenColumn = scanner->column;
    scanner->tokenOffset = scanner->offset;
}

static void syncSyntheticTokenPos(Scanner* scanner) {
    scanner->tokenLine = scanner->line;
    scanner->tokenColumn = scanner->column;
    scanner->tokenOffset = scanner->offset;
}

static Token buildToken(
    Scanner* scanner,
    TokenType type,
    const char* start,
    int length,
    int line,
    int column,
    int offset
) {
    Token token;
    (void)scanner;
    token.type = type;
    token.start = start;
    token.length = length;
    token.line = line;
    token.column = column;
    token.offset = offset;
    return token;
}

static Token makeToken(Scanner* scanner, TokenType type) {
    return buildToken(
        scanner,
        type,
        scanner->start,
        (int)(scanner->current - scanner->start),
        scanner->tokenLine,
        scanner->tokenColumn,
        scanner->tokenOffset
    );
}

static Token makeSyntheticToken(Scanner* scanner, TokenType type) {
    return buildToken(
        scanner,
        type,
        scanner->current,
        0,
        scanner->tokenLine,
        scanner->tokenColumn,
        scanner->tokenOffset
    );
}

static Token errorToken(Scanner* scanner, const char* message) {
    int len = 0;

    while (message[len] != '\0') {
        len++;
    }

    reportLexerError(
        scanner->diagnostics,
        scanner->tokenLine,
        scanner->tokenColumn,
        scanner->tokenOffset,
        message
    );

    return buildToken(
        scanner,
        TOKEN_ERROR,
        message,
        len,
        scanner->tokenLine,
        scanner->tokenColumn,
        scanner->tokenOffset
    );
}

static int isBlankOrCommentLine(Scanner* scanner) {
    const char* p = scanner->current;

    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }

    return *p == '\n' || *p == '#' || *p == '\0';
}

static int isDigit(char c) {
    return c >= '0' && c <= '9';
}

static int isAlpha(char c) {
    return ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '_');
}

static int isAlphaNumeric(char c) {
    return isAlpha(c) || isDigit(c);
}

static int stringsEqual(const char* a, int aLen, const char* b) {
    int i = 0;

    while (b[i] != '\0') {
        if (i >= aLen || a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return i == aLen;
}

static TokenType identifierType(Scanner* scanner) {
    int length = (int)(scanner->current - scanner->start);
    const char* text = scanner->start;

    if (stringsEqual(text, length, "if"))       return TOKEN_IF;
    if (stringsEqual(text, length, "elif"))     return TOKEN_ELIF;
    if (stringsEqual(text, length, "else"))     return TOKEN_ELSE;
    if (stringsEqual(text, length, "while"))    return TOKEN_WHILE;
    if (stringsEqual(text, length, "for"))      return TOKEN_FOR;
    if (stringsEqual(text, length, "in"))       return TOKEN_IN;
    if (stringsEqual(text, length, "def"))      return TOKEN_DEF;
    if (stringsEqual(text, length, "class"))    return TOKEN_CLASS;
    if (stringsEqual(text, length, "return"))   return TOKEN_RETURN;
    if (stringsEqual(text, length, "True"))     return TOKEN_TRUE;
    if (stringsEqual(text, length, "False"))    return TOKEN_FALSE;
    if (stringsEqual(text, length, "None"))     return TOKEN_NONE;
    if (stringsEqual(text, length, "and"))      return TOKEN_AND;
    if (stringsEqual(text, length, "or"))       return TOKEN_OR;
    if (stringsEqual(text, length, "not"))      return TOKEN_NOT;
    if (stringsEqual(text, length, "pass"))     return TOKEN_PASS;
    if (stringsEqual(text, length, "break"))    return TOKEN_BREAK;
    if (stringsEqual(text, length, "continue")) return TOKEN_CONTINUE;

    return TOKEN_IDENTIFIER;
}

static void skipInlineWhitespace(Scanner* scanner) {
    while (1) {
        char c = peekChar(scanner);
        if (c == ' ' || c == '\t' || c == '\r') {
            advanceChar(scanner);
        } else {
            break;
        }
    }
}

static void skipComment(Scanner* scanner) {
    while (!isAtEnd(scanner) && peekChar(scanner) != '\n') {
        advanceChar(scanner);
    }
}

static void skipLeadingWhitespaceOnCurrentLine(Scanner* scanner) {
    while (peekChar(scanner) == ' ' ||
           peekChar(scanner) == '\t' ||
           peekChar(scanner) == '\r') {
        advanceChar(scanner);
    }
}

static void skipBlankOrCommentOnlyLine(Scanner* scanner) {
    skipLeadingWhitespaceOnCurrentLine(scanner);

    if (peekChar(scanner) == '#') {
        skipComment(scanner);
    }

    if (peekChar(scanner) == '\n') {
        advanceChar(scanner);
    }
}

static int handleIndentation(Scanner* scanner, Token* outToken) {
    const char* probe;
    int indent = 0;
    int currentIndent;

    if (!scanner->atLineStart) return 0;
    if (scanner->groupDepth > 0) return 0;

    probe = scanner->current;
    while (*probe == ' ' || *probe == '\t') {
        indent += (*probe == ' ') ? 1 : TAB_WIDTH;
        probe++;
    }

    if (*probe == '\n' || *probe == '#' || *probe == '\0') {
        return 0;
    }

    while (peekChar(scanner) == ' ' || peekChar(scanner) == '\t') {
        advanceChar(scanner);
    }

    markTokenStart(scanner);
    currentIndent = scanner->indentStack[scanner->indentCount - 1];
    scanner->atLineStart = 0;

    if (indent > currentIndent) {
        if (scanner->indentCount >= MAX_INDENTS) {
            *outToken = errorToken(scanner, "Indentation stack overflow");
            return 1;
        }

        scanner->indentStack[scanner->indentCount++] = indent;
        *outToken = makeSyntheticToken(scanner, TOKEN_INDENT);
        return 1;
    }

    if (indent < currentIndent) {
        while (scanner->indentCount > 1 &&
               indent < scanner->indentStack[scanner->indentCount - 1]) {
            scanner->indentCount--;
            scanner->pendingDedents++;
        }

        if (indent != scanner->indentStack[scanner->indentCount - 1]) {
            *outToken = errorToken(scanner, "Inconsistent indentation");
            return 1;
        }

        if (scanner->pendingDedents > 0) {
            scanner->pendingDedents--;
            *outToken = makeSyntheticToken(scanner, TOKEN_DEDENT);
            return 1;
        }
    }

    return 0;
}

static Token identifier(Scanner* scanner) {
    while (isAlphaNumeric(peekChar(scanner))) {
        advanceChar(scanner);
    }
    return makeToken(scanner, identifierType(scanner));
}

static Token number(Scanner* scanner) {
    while (isDigit(peekChar(scanner))) {
        advanceChar(scanner);
    }

    if (peekChar(scanner) == '.' && isDigit(peekNextChar(scanner))) {
        advanceChar(scanner);
        while (isDigit(peekChar(scanner))) {
            advanceChar(scanner);
        }
    }

    return makeToken(scanner, TOKEN_NUMBER);
}

static Token string(Scanner* scanner, char quote) {
    while (!isAtEnd(scanner)) {
        char c = peekChar(scanner);

        if (c == quote) {
            advanceChar(scanner);
            return makeToken(scanner, TOKEN_STRING);
        }

        if (c == '\n') {
            return errorToken(scanner, "Unterminated string");
        }

        if (c == '\\') {
            advanceChar(scanner);
            if (isAtEnd(scanner)) {
                return errorToken(scanner, "Unterminated escape sequence");
            }
            advanceChar(scanner);
            continue;
        }

        advanceChar(scanner);
    }

    return errorToken(scanner, "Unterminated string");
}

static Token scanOperatorOrPunctuation(Scanner* scanner, char c) {
    switch (c) {
        case '(':
            scanner->groupDepth++;
            return makeToken(scanner, TOKEN_LPAREN);

        case ')':
            if (scanner->groupDepth > 0) scanner->groupDepth--;
            return makeToken(scanner, TOKEN_RPAREN);

        case '[':
            scanner->groupDepth++;
            return makeToken(scanner, TOKEN_LBRACKET);

        case ']':
            if (scanner->groupDepth > 0) scanner->groupDepth--;
            return makeToken(scanner, TOKEN_RBRACKET);

        case ',':
            return makeToken(scanner, TOKEN_COMMA);

        case '.':
            return makeToken(scanner, TOKEN_DOT);

        case ':':
            return makeToken(scanner, TOKEN_COLON);

        case '+':
            return makeToken(scanner, TOKEN_PLUS);

        case '-':
            return makeToken(scanner, TOKEN_MINUS);

        case '*':
            return makeToken(scanner, TOKEN_STAR);

        case '/':
            return makeToken(scanner, TOKEN_SLASH);

        case '%':
            return makeToken(scanner, TOKEN_PERCENT);

        case '=':
            return makeToken(
                scanner,
                matchChar(scanner, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL
            );

        case '<':
            return makeToken(
                scanner,
                matchChar(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS
            );

        case '>':
            return makeToken(
                scanner,
                matchChar(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER
            );

        case '!':
            if (matchChar(scanner, '=')) {
                return makeToken(scanner, TOKEN_NOT_EQUAL);
            }
            return errorToken(scanner, "Unexpected character '!'. Use 'not' or '!='");
    }

    return errorToken(scanner, "Unexpected character");
}

static int emitPendingDedent(Scanner* scanner, Token* outToken) {
    if (scanner->pendingDedents <= 0) {
        return 0;
    }

    syncSyntheticTokenPos(scanner);
    scanner->pendingDedents--;
    *outToken = makeSyntheticToken(scanner, TOKEN_DEDENT);
    return 1;
}

static int emitEOFOrTrailingDedent(Scanner* scanner, Token* outToken) {
    if (!isAtEnd(scanner)) {
        return 0;
    }

    if (scanner->indentCount > 1) {
        scanner->indentCount--;
        syncSyntheticTokenPos(scanner);
        *outToken = makeSyntheticToken(scanner, TOKEN_DEDENT);
        return 1;
    }

    markTokenStart(scanner);
    *outToken = makeToken(scanner, TOKEN_EOF);
    return 1;
}

static void skipIgnoredInput(Scanner* scanner) {
    for (;;) {
        if (isAtEnd(scanner)) {
            return;
        }

        if (scanner->atLineStart &&
            scanner->groupDepth == 0 &&
            isBlankOrCommentLine(scanner)) {
            skipBlankOrCommentOnlyLine(scanner);
            continue;
        }

        markTokenStart(scanner);

        if (!(scanner->atLineStart && scanner->groupDepth == 0)) {
            if (peekChar(scanner) == ' ' ||
                peekChar(scanner) == '\t' ||
                peekChar(scanner) == '\r') {
                skipInlineWhitespace(scanner);
                continue;
            }
        }

        if (peekChar(scanner) == '#') {
            skipComment(scanner);
            continue;
        }

        return;
    }
}

static Token scanSingleToken(Scanner* scanner) {
    char c;

    markTokenStart(scanner);
    c = advanceChar(scanner);

    if (c == '\n') {
        if (scanner->groupDepth > 0) {
            return errorToken(scanner, "Internal lexer state error on newline suppression");
        }
        return makeToken(scanner, TOKEN_NEWLINE);
    }

    scanner->atLineStart = 0;

    if (isAlpha(c)) return identifier(scanner);
    if (isDigit(c)) return number(scanner);
    if (c == '"' || c == '\'') return string(scanner, c);

    return scanOperatorOrPunctuation(scanner, c);
}

static Token scanToken(Scanner* scanner) {
    Token token;

    if (emitPendingDedent(scanner, &token)) {
        return token;
    }

    if (emitEOFOrTrailingDedent(scanner, &token)) {
        return token;
    }

    for (;;) {
        Token indentToken;

        skipIgnoredInput(scanner);

        if (emitPendingDedent(scanner, &token)) {
            return token;
        }

        if (emitEOFOrTrailingDedent(scanner, &token)) {
            return token;
        }

        if (handleIndentation(scanner, &indentToken)) {
            return indentToken;
        }

        if (peekChar(scanner) == '\n' && scanner->groupDepth > 0) {
            advanceChar(scanner);
            continue;
        }

        break;
    }

    return scanSingleToken(scanner);
}

static void lexAllTokens(Scanner* scanner, TokenArray* outTokens) {
    for (;;) {
        Token token = scanToken(scanner);
        pushToken(outTokens, token);

        if (token.type == TOKEN_ERROR || token.type == TOKEN_EOF) {
            break;
        }
    }
}

void normalizeTokens(TokenArray* tokens) {
    (void)tokens;
}

const char* tokenTypeToString(TokenType type) {
    switch (type) {
        case TOKEN_EOF:           return "EOF";
        case TOKEN_ERROR:         return "ERROR";
        case TOKEN_NEWLINE:       return "NEWLINE";
        case TOKEN_INDENT:        return "INDENT";
        case TOKEN_DEDENT:        return "DEDENT";

        case TOKEN_IDENTIFIER:    return "IDENTIFIER";
        case TOKEN_NUMBER:        return "NUMBER";
        case TOKEN_STRING:        return "STRING";

        case TOKEN_LPAREN:        return "LPAREN";
        case TOKEN_RPAREN:        return "RPAREN";
        case TOKEN_LBRACKET:      return "LBRACKET";
        case TOKEN_RBRACKET:      return "RBRACKET";
        case TOKEN_COMMA:         return "COMMA";
        case TOKEN_DOT:           return "DOT";
        case TOKEN_COLON:         return "COLON";

        case TOKEN_PLUS:          return "PLUS";
        case TOKEN_MINUS:         return "MINUS";
        case TOKEN_STAR:          return "STAR";
        case TOKEN_SLASH:         return "SLASH";
        case TOKEN_PERCENT:       return "PERCENT";
        case TOKEN_EQUAL:         return "EQUAL";
        case TOKEN_EQUAL_EQUAL:   return "EQUAL_EQUAL";
        case TOKEN_NOT_EQUAL:     return "NOT_EQUAL";
        case TOKEN_LESS:          return "LESS";
        case TOKEN_LESS_EQUAL:    return "LESS_EQUAL";
        case TOKEN_GREATER:       return "GREATER";
        case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";

        case TOKEN_IF:            return "IF";
        case TOKEN_ELIF:          return "ELIF";
        case TOKEN_ELSE:          return "ELSE";
        case TOKEN_WHILE:         return "WHILE";
        case TOKEN_FOR:           return "FOR";
        case TOKEN_IN:            return "IN";
        case TOKEN_DEF:           return "DEF";
        case TOKEN_CLASS:         return "CLASS";
        case TOKEN_RETURN:        return "RETURN";
        case TOKEN_TRUE:          return "TRUE";
        case TOKEN_FALSE:         return "FALSE";
        case TOKEN_NONE:          return "NONE";
        case TOKEN_AND:           return "AND";
        case TOKEN_OR:            return "OR";
        case TOKEN_NOT:           return "NOT";
        case TOKEN_PASS:          return "PASS";
        case TOKEN_BREAK:         return "BREAK";
        case TOKEN_CONTINUE:      return "CONTINUE";
    }

    return "INVALID";
}

void printToken(const Token* token) {
    printf("%-14s line %-3d col %-3d off %-3d text: %.*s\n",
           tokenTypeToString(token->type),
           token->line,
           token->column,
           token->offset,
           token->length,
           token->start);
}

void printTokenArray(const TokenArray* array) {
    int i;
    for (i = 0; i < array->count; i++) {
        printToken(&array->data[i]);
    }
}

void printDiagnosticsSummary(const Diagnostics* diagnostics) {
    printf("\n=== DIAGNOSTICS ===\n");
    printf("Errors: %d\n", diagnostics->errorCount);
    printf("Warnings: %d\n", diagnostics->warningCount);
}

int lexSource(const char* source, TokenArray* outTokens, Diagnostics* diagnostics) {
    Scanner scanner;

    initScanner(&scanner, source, diagnostics);
    initTokenArray(outTokens);
    lexAllTokens(&scanner, outTokens);

    return diagnostics->errorCount == 0;
}