#include "runtime.h"
#include "builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* copyTokenText(Token token) {
    char* text = (char*)malloc((size_t)token.length + 1);
    if (text == NULL) {
        return NULL;
    }

    memcpy(text, token.start, (size_t)token.length);
    text[token.length] = '\0';
    return text;
}

static int isNumericBinaryOperator(TokenType type) {
    switch (type) {
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_PERCENT:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
            return 1;
        default:
            return 0;
    }
}

static int requireNumberOperands(Runtime* runtime, const Value* left, const Value* right) {
    if (left->type != VAL_NUMBER || right->type != VAL_NUMBER) {
        runtimeError(runtime, "Operator requires number operands.");
        return 0;
    }
    return 1;
}

static Value evalLiteral(Runtime* runtime, Token literal) {
    char* text;
    char* endPtr;
    double numberValue;

    switch (literal.type) {
        case TOKEN_NUMBER:
            text = copyTokenText(literal);
            if (text == NULL) {
                runtimeError(runtime, "Out of memory while parsing number literal.");
                return makeNone();
            }

            numberValue = strtod(text, &endPtr);

            if (endPtr == text) {
                free(text);
                runtimeError(runtime, "Invalid number literal.");
                return makeNone();
            }

            while (*endPtr == ' ' || *endPtr == '\t' || *endPtr == '\r' || *endPtr == '\n') {
                endPtr++;
            }

            if (*endPtr != '\0') {
                free(text);
                runtimeError(runtime, "Invalid number literal.");
                return makeNone();
            }

            free(text);
            return makeNumber(numberValue);

        case TOKEN_TRUE:
            return makeBool(1);

        case TOKEN_FALSE:
            return makeBool(0);

        case TOKEN_NONE:
            return makeNone();

        case TOKEN_STRING: {
            char* raw = copyTokenText(literal);
            char* cooked;
            int cookedLength;

            if (raw == NULL) {
                runtimeError(runtime, "Out of memory while parsing string literal.");
                return makeNone();
            }

            if (literal.length >= 2 &&
                ((raw[0] == '"' && raw[literal.length - 1] == '"') ||
                 (raw[0] == '\'' && raw[literal.length - 1] == '\''))) {
                raw[literal.length - 1] = '\0';
                cooked = (char*)malloc((size_t)literal.length - 1);
                if (cooked == NULL) {
                    free(raw);
                    runtimeError(runtime, "Out of memory while parsing string literal.");
                    return makeNone();
                }

                memcpy(cooked, raw + 1, (size_t)literal.length - 2);
                cookedLength = literal.length - 2;
                cooked[cookedLength] = '\0';
                free(raw);
                return makeStringOwned(cooked);
            }

            return makeStringOwned(raw);
        }

        default:
            runtimeError(runtime, "Unsupported literal token.");
            return makeNone();
    }
}

static Value evalUnary(Runtime* runtime, AstNode* node) {
    Value operand = runtimeEvalExpression(runtime, node->as.unaryExpr.operand);
    Value result = makeNone();

    if (runtime->hadError) {
        freeValue(&operand);
        return makeNone();
    }

    switch (node->as.unaryExpr.op.type) {
        case TOKEN_MINUS:
            if (operand.type != VAL_NUMBER) {
                runtimeError(runtime, "Unary '-' requires a number.");
            } else {
                result = makeNumber(-operand.as.number);
            }
            break;

        case TOKEN_NOT:
            result = makeBool(!valueIsTruthy(&operand));
            break;

        default:
            runtimeError(runtime, "Unsupported unary operator.");
            break;
    }

    freeValue(&operand);
    return result;
}

static Value evalBinary(Runtime* runtime, AstNode* node) {
    TokenType opType = node->as.binaryExpr.op.type;
    Value left;
    Value right;
    Value result = makeNone();

    if (opType == TOKEN_AND) {
        left = runtimeEvalExpression(runtime, node->as.binaryExpr.left);
        if (runtime->hadError) {
            return makeNone();
        }

        if (!valueIsTruthy(&left)) {
            result = makeBool(0);
            freeValue(&left);
            return result;
        }

        freeValue(&left);
        right = runtimeEvalExpression(runtime, node->as.binaryExpr.right);
        if (runtime->hadError) {
            return makeNone();
        }

        result = makeBool(valueIsTruthy(&right));
        freeValue(&right);
        return result;
    }

    if (opType == TOKEN_OR) {
        left = runtimeEvalExpression(runtime, node->as.binaryExpr.left);
        if (runtime->hadError) {
            return makeNone();
        }

        if (valueIsTruthy(&left)) {
            result = makeBool(1);
            freeValue(&left);
            return result;
        }

        freeValue(&left);
        right = runtimeEvalExpression(runtime, node->as.binaryExpr.right);
        if (runtime->hadError) {
            return makeNone();
        }

        result = makeBool(valueIsTruthy(&right));
        freeValue(&right);
        return result;
    }

    left = runtimeEvalExpression(runtime, node->as.binaryExpr.left);
    if (runtime->hadError) {
        return makeNone();
    }

    right = runtimeEvalExpression(runtime, node->as.binaryExpr.right);
    if (runtime->hadError) {
        freeValue(&left);
        return makeNone();
    }

    switch (opType) {
        case TOKEN_PLUS:
            if (left.type == VAL_NUMBER && right.type == VAL_NUMBER) {
                result = makeNumber(left.as.number + right.as.number);
            } else if (left.type == VAL_STRING && right.type == VAL_STRING) {
                size_t leftLen = strlen(left.as.string ? left.as.string : "");
                size_t rightLen = strlen(right.as.string ? right.as.string : "");
                char* combined = (char*)malloc(leftLen + rightLen + 1);

                if (combined == NULL) {
                    runtimeError(runtime, "Out of memory while concatenating strings.");
                } else {
                    memcpy(combined, left.as.string ? left.as.string : "", leftLen);
                    memcpy(combined + leftLen, right.as.string ? right.as.string : "", rightLen);
                    combined[leftLen + rightLen] = '\0';
                    result = makeStringOwned(combined);
                }
            } else {
                runtimeError(runtime, "Operator '+' requires two numbers or two strings.");
            }
            break;

        case TOKEN_MINUS:
            if (requireNumberOperands(runtime, &left, &right)) {
                result = makeNumber(left.as.number - right.as.number);
            }
            break;

        case TOKEN_STAR:
            if (requireNumberOperands(runtime, &left, &right)) {
                result = makeNumber(left.as.number * right.as.number);
            }
            break;

        case TOKEN_SLASH:
            if (requireNumberOperands(runtime, &left, &right)) {
                if (right.as.number == 0.0) {
                    runtimeError(runtime, "Division by zero.");
                } else {
                    result = makeNumber(left.as.number / right.as.number);
                }
            }
            break;

        case TOKEN_PERCENT:
            if (requireNumberOperands(runtime, &left, &right)) {
                long long a = (long long)left.as.number;
                long long b = (long long)right.as.number;

                if (b == 0) {
                    runtimeError(runtime, "Modulo by zero.");
                } else {
                    result = makeNumber((double)(a % b));
                }
            }
            break;

        case TOKEN_EQUAL_EQUAL:
            result = makeBool(valueEquals(&left, &right));
            break;

        case TOKEN_NOT_EQUAL:
            result = makeBool(!valueEquals(&left, &right));
            break;

        case TOKEN_LESS:
            if (requireNumberOperands(runtime, &left, &right)) {
                result = makeBool(left.as.number < right.as.number);
            }
            break;

        case TOKEN_LESS_EQUAL:
            if (requireNumberOperands(runtime, &left, &right)) {
                result = makeBool(left.as.number <= right.as.number);
            }
            break;

        case TOKEN_GREATER:
            if (requireNumberOperands(runtime, &left, &right)) {
                result = makeBool(left.as.number > right.as.number);
            }
            break;

        case TOKEN_GREATER_EQUAL:
            if (requireNumberOperands(runtime, &left, &right)) {
                result = makeBool(left.as.number >= right.as.number);
            }
            break;

        default:
            runtimeError(runtime, "Unsupported binary operator.");
            break;
    }

    freeValue(&left);
    freeValue(&right);
    return result;
}

static ExecResult executeAssign(Runtime* runtime, AstNode* node) {
    AstNode* target = node->as.assignStmt.target;
    Value rhs;

    if (target == NULL) {
        runtimeError(runtime, "Assignment target is null.");
        return execError();
    }

    rhs = runtimeEvalExpression(runtime, node->as.assignStmt.value);
    if (runtime->hadError) {
        return execError();
    }

    if (target->type == AST_IDENTIFIER_EXPR) {
        char* name = copyTokenText(target->as.identifierExpr.name);

        if (name == NULL) {
            freeValue(&rhs);
            runtimeError(runtime, "Out of memory while assigning variable.");
            return execError();
        }

        if (!envAssign(runtime->current, name, rhs)) {
            if (!envDefine(runtime->current, name, rhs)) {
                free(name);
                freeValue(&rhs);
                runtimeError(runtime, "Failed to assign variable.");
                return execError();
            }
        }

        printf("[assign] %s = ", name);
        printValue(&rhs);
        printf("\n");

        free(name);
        freeValue(&rhs);
        return execNormal();
    }

    freeValue(&rhs);
    runtimeError(runtime, "Only identifier assignment is supported right now.");
    return execError();
}

void runtimeInit(Runtime* runtime) {
    envInit(&runtime->globals, NULL);
    runtime->current = &runtime->globals;
    runtime->hadError = 0;
    runtime->errorMessage[0] = '\0';

    registerBuiltins(&runtime->globals);
}

void runtimeFree(Runtime* runtime) {
    envFree(&runtime->globals);
    runtime->current = NULL;
}

void runtimeError(Runtime* runtime, const char* message) {
    runtime->hadError = 1;

    if (message == NULL) {
        runtime->errorMessage[0] = '\0';
        return;
    }

    strncpy(runtime->errorMessage, message, sizeof(runtime->errorMessage) - 1);
    runtime->errorMessage[sizeof(runtime->errorMessage) - 1] = '\0';
}

ExecResult execNormal(void) {
    ExecResult result;
    result.type = FLOW_NORMAL;
    result.value = makeNone();
    return result;
}

ExecResult execError(void) {
    ExecResult result;
    result.type = FLOW_ERROR;
    result.value = makeNone();
    return result;
}

ExecResult execReturn(Value value) {
    ExecResult result;
    result.type = FLOW_RETURN;
    result.value = copyValue(&value);
    return result;
}

Value runtimeEvalExpression(Runtime* runtime, AstNode* node) {
    Value valueOut;
    char* name;

    if (node == NULL) {
        runtimeError(runtime, "Tried to evaluate a null expression node.");
        return makeNone();
    }

    switch (node->type) {
        case AST_LITERAL_EXPR:
            return evalLiteral(runtime, node->as.literalExpr.literal);

        case AST_IDENTIFIER_EXPR:
            name = copyTokenText(node->as.identifierExpr.name);
            if (name == NULL) {
                runtimeError(runtime, "Out of memory while reading identifier.");
                return makeNone();
            }

            if (!envGet(runtime->current, name, &valueOut)) {
                free(name);
                runtimeError(runtime, "Undefined variable.");
                return makeNone();
            }

            free(name);
            return valueOut;

        case AST_GROUPING_EXPR:
            return runtimeEvalExpression(runtime, node->as.groupingExpr.expression);

        case AST_UNARY_EXPR:
            return evalUnary(runtime, node);

        case AST_BINARY_EXPR:
            return evalBinary(runtime, node);

        case AST_CALL_EXPR:
            runtimeError(runtime, "Function calls are not supported yet.");
            return makeNone();

        case AST_MEMBER_EXPR:
            runtimeError(runtime, "Member access is not supported yet.");
            return makeNone();

        default:
            runtimeError(runtime, "Unsupported expression node.");
            return makeNone();
    }
}

ExecResult runtimeExecuteNode(Runtime* runtime, AstNode* node) {
    int i;
    Value temp;
    ExecResult result;
    Value conditionValue;

    if (node == NULL) {
        return execNormal();
    }

    switch (node->type) {
        case AST_MODULE:
            for (i = 0; i < node->as.module.statements.count; i++) {
                result = runtimeExecuteNode(runtime, node->as.module.statements.items[i]);
                if (result.type != FLOW_NORMAL) {
                    return result;
                }
            }
            return execNormal();

        case AST_BLOCK:
            for (i = 0; i < node->as.block.statements.count; i++) {
                result = runtimeExecuteNode(runtime, node->as.block.statements.items[i]);
                if (result.type != FLOW_NORMAL) {
                    return result;
                }
            }
            return execNormal();

        case AST_EXPR_STMT:
            temp = runtimeEvalExpression(runtime, node->as.exprStmt.expression);
            freeValue(&temp);
            return runtime->hadError ? execError() : execNormal();

        case AST_ASSIGN_STMT:
            return executeAssign(runtime, node);

        case AST_RETURN_STMT:
            temp = runtimeEvalExpression(runtime, node->as.returnStmt.value);
            if (runtime->hadError) {
                return execError();
            }
            result = execReturn(temp);
            freeValue(&temp);
            return result;

        case AST_IF_STMT:
            conditionValue = runtimeEvalExpression(runtime, node->as.ifStmt.condition);
            if (runtime->hadError) {
                return execError();
            }

            if (valueIsTruthy(&conditionValue)) {
                freeValue(&conditionValue);
                return runtimeExecuteNode(runtime, node->as.ifStmt.thenBlock);
            }

            freeValue(&conditionValue);
            return runtimeExecuteNode(runtime, node->as.ifStmt.elseBranch);

        case AST_WHILE_STMT:
            for (;;) {
                conditionValue = runtimeEvalExpression(runtime, node->as.whileStmt.condition);
                if (runtime->hadError) {
                    return execError();
                }

                if (!valueIsTruthy(&conditionValue)) {
                    freeValue(&conditionValue);
                    break;
                }

                freeValue(&conditionValue);
                result = runtimeExecuteNode(runtime, node->as.whileStmt.body);

                if (result.type == FLOW_BREAK) {
                    return execNormal();
                }

                if (result.type == FLOW_CONTINUE) {
                    continue;
                }

                if (result.type != FLOW_NORMAL) {
                    return result;
                }
            }
            return execNormal();

        case AST_PASS_STMT:
            return execNormal();

        case AST_BREAK_STMT:
            result.type = FLOW_BREAK;
            result.value = makeNone();
            return result;

        case AST_CONTINUE_STMT:
            result.type = FLOW_CONTINUE;
            result.value = makeNone();
            return result;

        case AST_FUNCTION_DEF:
            runtimeError(runtime, "Function definitions are not supported yet.");
            return execError();

        case AST_CLASS_DEF:
            runtimeError(runtime, "Class definitions are not supported yet.");
            return execError();

        case AST_FOR_STMT:
            runtimeError(runtime, "For-loops are not supported yet.");
            return execError();

        default:
            runtimeError(runtime, "Unhandled AST node in runtime.");
            return execError();
    }
}