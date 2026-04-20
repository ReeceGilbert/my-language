#ifndef RUNTIME_H
#define RUNTIME_H

#include "env.h"
#include "ast.h"

typedef enum {
    FLOW_NORMAL,
    FLOW_RETURN,
    FLOW_BREAK,
    FLOW_CONTINUE,
    FLOW_ERROR
} FlowType;

typedef struct {
    FlowType type;
    Value value;
} ExecResult;

typedef struct {
    Environment globals;
    Environment* current;
    int hadError;
    char errorMessage[512];
} Runtime;

void runtimeInit(Runtime* runtime);
void runtimeFree(Runtime* runtime);

void runtimeError(Runtime* runtime, const char* message);

ExecResult execNormal(void);
ExecResult execError(void);
ExecResult execReturn(Value value);

Value runtimeEvalExpression(Runtime* runtime, AstNode* node);
ExecResult runtimeExecuteNode(Runtime* runtime, AstNode* node);

#endif