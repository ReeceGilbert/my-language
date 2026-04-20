#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "ast.h"

AstNode* parseTokens(TokenArray* tokens);

#endif