#ifndef PARSER_H
#define PARSER_H

#include "token_shared.h"

typedef struct AstNode AstNode;

AstNode* parseTokens(TokenArray* tokens);
void printAst(AstNode* node, int depth);

#endif