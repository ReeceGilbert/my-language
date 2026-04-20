// lexer.h
#ifndef LEXER_H
#define LEXER_H

#include "token.h"

typedef struct {
    int errorCount;
    int warningCount;
} Diagnostics;

void initDiagnostics(Diagnostics* diagnostics);
void printDiagnosticsSummary(const Diagnostics* diagnostics);

void initTokenArray(TokenArray* array);
void freeTokenArray(TokenArray* array);

int lexSource(const char* source, TokenArray* outTokens, Diagnostics* diagnostics);
void normalizeTokens(TokenArray* tokens);

const char* tokenTypeToString(TokenType type);
void printToken(const Token* token);
void printTokenArray(const TokenArray* array);

#endif