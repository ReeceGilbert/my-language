#include "runtime.h"
#include "builtins.h"
#include "env.h"

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

static Token makeSyntheticIdentifierToken(const char* text) {
    Token token;

    token.type = TOKEN_IDENTIFIER;
    token.start = text;
    token.length = (int)strlen(text);
    token.line = 0;
    token.column = 0;
    token.offset = 0;

    return token;
}

static int assignLoopVariable(Runtime* runtime, Token nameToken, Value value) {
    char* name = copyTokenText(nameToken);

    if (name == NULL) {
        runtimeError(runtime, "Out of memory while assigning loop variable.");
        return 0;
    }

    if (!envAssign(runtime->current, name, value)) {
        if (!envDefine(runtime->current, name, value)) {
            free(name);
            runtimeError(runtime, "Failed to assign loop variable.");
            return 0;
        }
    }

    free(name);
    return 1;
}

static int requireListIterable(Runtime* runtime, const Value* iterable) {
    if (iterable->type != VAL_LIST) {
        runtimeError(runtime, "For-loop iterable must be a list.");
        return 0;
    }
    return 1;
}

static ExecResult executeForList(Runtime* runtime, AstNode* loopTarget, Value iterableValue, AstNode* body) {
    Token loopVar;
    int i;
    ExecResult result;

    if (loopTarget == NULL || loopTarget->type != AST_IDENTIFIER_EXPR) {
        runtimeError(runtime, "For-loop target must be an identifier.");
        return execError();
    }

    if (!requireListIterable(runtime, &iterableValue)) {
        return execError();
    }

    loopVar = loopTarget->as.identifierExpr.name;

    for (i = 0; i < iterableValue.as.list->count; i++) {
        Value elementCopy = copyValue(&iterableValue.as.list->items[i]);

        if (!assignLoopVariable(runtime, loopVar, elementCopy)) {
            freeValue(&elementCopy);
            return execError();
        }

        freeValue(&elementCopy);

        result = runtimeExecuteNode(runtime, body);

        if (result.type == FLOW_BREAK) {
            freeValue(&result.value);
            return execNormal();
        }

        if (result.type == FLOW_CONTINUE) {
            freeValue(&result.value);
            continue;
        }

        if (result.type != FLOW_NORMAL) {
            return result;
        }
    }

    return execNormal();
}

static int requireNumberOperands(Runtime* runtime, const Value* left, const Value* right) {
    if (left->type != VAL_NUMBER || right->type != VAL_NUMBER) {
        runtimeError(runtime, "Operator requires number operands.");
        return 0;
    }
    return 1;
}

static Token* copyParameterTokens(const TokenList* list) {
    Token* params;
    int i;

    if (list == NULL || list->count <= 0) {
        return NULL;
    }

    params = (Token*)malloc(sizeof(Token) * (size_t)list->count);
    if (params == NULL) {
        return NULL;
    }

    for (i = 0; i < list->count; i++) {
        params[i] = list->items[i];
    }

    return params;
}

static FunctionObject* createFunctionObject(Runtime* runtime, AstNode* node) {
    FunctionObject* function;
    char* nameCopy;
    Token* paramsCopy;

    if (node == NULL || node->type != AST_FUNCTION_DEF) {
        runtimeError(runtime, "Invalid function definition node.");
        return NULL;
    }

    nameCopy = copyTokenText(node->as.functionDef.name);
    if (nameCopy == NULL) {
        runtimeError(runtime, "Out of memory while creating function.");
        return NULL;
    }

    paramsCopy = copyParameterTokens(&node->as.functionDef.parameters);
    if (node->as.functionDef.parameters.count > 0 && paramsCopy == NULL) {
        free(nameCopy);
        runtimeError(runtime, "Out of memory while copying function parameters.");
        return NULL;
    }

    function = (FunctionObject*)malloc(sizeof(FunctionObject));
    if (function == NULL) {
        free(nameCopy);
        free(paramsCopy);
        runtimeError(runtime, "Out of memory while creating function.");
        return NULL;
    }

    function->name = nameCopy;
    function->params = paramsCopy;
    function->paramCount = node->as.functionDef.parameters.count;
    function->body = node->as.functionDef.body;
    function->closure = runtime->current;
    return function;
}

static int ensureMethodCapacity(ClassObject* classObject) {
    MethodEntry* newMethods;
    int newCapacity;

    if (classObject == NULL) {
        return 0;
    }

    if (classObject->methodCount < classObject->methodCapacity) {
        return 1;
    }

    newCapacity = (classObject->methodCapacity < 8) ? 8 : classObject->methodCapacity * 2;
    newMethods = (MethodEntry*)realloc(classObject->methods, sizeof(MethodEntry) * (size_t)newCapacity);
    if (newMethods == NULL) {
        return 0;
    }

    classObject->methods = newMethods;
    classObject->methodCapacity = newCapacity;
    return 1;
}

static int classAddMethod(Runtime* runtime, ClassObject* classObject, FunctionObject* function) {
    char* nameCopy;

    if (classObject == NULL || function == NULL || function->name == NULL) {
        runtimeError(runtime, "Invalid method while building class.");
        return 0;
    }

    if (!ensureMethodCapacity(classObject)) {
        runtimeError(runtime, "Out of memory while growing class methods.");
        return 0;
    }

    nameCopy = (char*)malloc(strlen(function->name) + 1);
    if (nameCopy == NULL) {
        runtimeError(runtime, "Out of memory while storing method name.");
        return 0;
    }

    strcpy(nameCopy, function->name);

    classObject->methods[classObject->methodCount].name = nameCopy;
    classObject->methods[classObject->methodCount].function = function;
    classObject->methodCount++;
    return 1;
}

static FunctionObject* classFindMethod(ClassObject* classObject, Token member) {
    char* memberName;
    FunctionObject* found = NULL;
    int i;

    if (classObject == NULL) {
        return NULL;
    }

    memberName = copyTokenText(member);
    if (memberName == NULL) {
        return NULL;
    }

    for (i = 0; i < classObject->methodCount; i++) {
        if (strcmp(classObject->methods[i].name, memberName) == 0) {
            found = classObject->methods[i].function;
            break;
        }
    }

    free(memberName);
    return found;
}

static BoundMethodObject* createBoundMethod(Runtime* runtime, InstanceObject* receiver, FunctionObject* method) {
    BoundMethodObject* boundMethod;

    if (receiver == NULL || method == NULL) {
        runtimeError(runtime, "Invalid bound method creation.");
        return NULL;
    }

    boundMethod = (BoundMethodObject*)malloc(sizeof(BoundMethodObject));
    if (boundMethod == NULL) {
        runtimeError(runtime, "Out of memory while creating bound method.");
        return NULL;
    }

    boundMethod->receiver = receiver;
    boundMethod->method = method;
    return boundMethod;
}

static ClassObject* createClassObject(Runtime* runtime, AstNode* node) {
    ClassObject* classObject;
    char* nameCopy;
    int i;

    if (node == NULL || node->type != AST_CLASS_DEF) {
        runtimeError(runtime, "Invalid class definition node.");
        return NULL;
    }

    nameCopy = copyTokenText(node->as.classDef.name);
    if (nameCopy == NULL) {
        runtimeError(runtime, "Out of memory while creating class.");
        return NULL;
    }

    classObject = (ClassObject*)malloc(sizeof(ClassObject));
    if (classObject == NULL) {
        free(nameCopy);
        runtimeError(runtime, "Out of memory while creating class.");
        return NULL;
    }

    classObject->name = nameCopy;
    classObject->body = node->as.classDef.body;
    classObject->closure = runtime->current;
    classObject->methods = NULL;
    classObject->methodCount = 0;
    classObject->methodCapacity = 0;

    if (node->as.classDef.body != NULL && node->as.classDef.body->type == AST_BLOCK) {
        for (i = 0; i < node->as.classDef.body->as.block.statements.count; i++) {
            AstNode* stmt = node->as.classDef.body->as.block.statements.items[i];
            if (stmt != NULL && stmt->type == AST_FUNCTION_DEF) {
                FunctionObject* method = createFunctionObject(runtime, stmt);
                if (method == NULL) {
                    return NULL;
                }

                if (!classAddMethod(runtime, classObject, method)) {
                    return NULL;
                }
            }
        }
    }

    return classObject;
}

static InstanceObject* createInstanceObject(Runtime* runtime, ClassObject* classObject) {
    InstanceObject* instance;

    if (classObject == NULL) {
        runtimeError(runtime, "Cannot instantiate a null class.");
        return NULL;
    }

    instance = (InstanceObject*)malloc(sizeof(InstanceObject));
    if (instance == NULL) {
        runtimeError(runtime, "Out of memory while creating instance.");
        return NULL;
    }

    instance->classObject = classObject;
    instance->fields = NULL;
    instance->fieldCount = 0;
    instance->fieldCapacity = 0;
    return instance;
}

static int ensureInstanceFieldCapacity(InstanceObject* instance) {
    InstanceField* newFields;
    int newCapacity;

    if (instance == NULL) {
        return 0;
    }

    if (instance->fieldCount < instance->fieldCapacity) {
        return 1;
    }

    newCapacity = (instance->fieldCapacity < 8) ? 8 : instance->fieldCapacity * 2;
    newFields = (InstanceField*)realloc(instance->fields, sizeof(InstanceField) * (size_t)newCapacity);
    if (newFields == NULL) {
        return 0;
    }

    instance->fields = newFields;
    instance->fieldCapacity = newCapacity;
    return 1;
}

static int instanceSetField(Runtime* runtime, InstanceObject* instance, Token member, Value value) {
    char* memberName;
    int i;

    if (instance == NULL) {
        runtimeError(runtime, "Cannot assign field on null instance.");
        return 0;
    }

    memberName = copyTokenText(member);
    if (memberName == NULL) {
        runtimeError(runtime, "Out of memory while assigning member.");
        return 0;
    }

    for (i = 0; i < instance->fieldCount; i++) {
        if (strcmp(instance->fields[i].name, memberName) == 0) {
            free(memberName);
            freeValue(&instance->fields[i].value);
            instance->fields[i].value = copyValue(&value);
            return 1;
        }
    }

    if (!ensureInstanceFieldCapacity(instance)) {
        free(memberName);
        runtimeError(runtime, "Out of memory while growing instance fields.");
        return 0;
    }

    instance->fields[instance->fieldCount].name = memberName;
    instance->fields[instance->fieldCount].value = copyValue(&value);
    instance->fieldCount++;
    return 1;
}

static int instanceGetField(Runtime* runtime, InstanceObject* instance, Token member, Value* outValue) {
    char* memberName;
    int i;

    if (instance == NULL || outValue == NULL) {
        runtimeError(runtime, "Cannot read field from null instance.");
        return 0;
    }

    memberName = copyTokenText(member);
    if (memberName == NULL) {
        runtimeError(runtime, "Out of memory while reading member.");
        return 0;
    }

    for (i = 0; i < instance->fieldCount; i++) {
        if (strcmp(instance->fields[i].name, memberName) == 0) {
            free(memberName);
            *outValue = copyValue(&instance->fields[i].value);
            return 1;
        }
    }

    free(memberName);
    runtimeError(runtime, "Undefined member.");
    return 0;
}

static Value* evaluateArguments(Runtime* runtime, AstNodeArray* arguments, int* outCount) {
    Value* values;
    int i;

    *outCount = 0;

    if (arguments == NULL || arguments->count <= 0) {
        return NULL;
    }

    values = (Value*)malloc(sizeof(Value) * (size_t)arguments->count);
    if (values == NULL) {
        runtimeError(runtime, "Out of memory while evaluating call arguments.");
        return NULL;
    }

    for (i = 0; i < arguments->count; i++) {
        values[i] = runtimeEvalExpression(runtime, arguments->items[i]);
        if (runtime->hadError) {
            int j;
            for (j = 0; j < i; j++) {
                freeValue(&values[j]);
            }
            free(values);
            return NULL;
        }
    }

    *outCount = arguments->count;
    return values;
}

static Value callUserFunction(Runtime* runtime, FunctionObject* function, int argCount, Value* args) {
    Environment localEnv;
    Environment* previousEnv;
    ExecResult result;
    Value returnValue;
    int i;

    if (function == NULL) {
        runtimeError(runtime, "Tried to call a null function.");
        return makeNone();
    }

    if (argCount != function->paramCount) {
        runtimeError(runtime, "Wrong number of arguments for function call.");
        return makeNone();
    }

    envInit(&localEnv, function->closure);
    previousEnv = runtime->current;
    runtime->current = &localEnv;

    for (i = 0; i < function->paramCount; i++) {
        char* paramName = copyTokenText(function->params[i]);
        if (paramName == NULL) {
            runtime->current = previousEnv;
            envFree(&localEnv);
            runtimeError(runtime, "Out of memory while binding function parameter.");
            return makeNone();
        }

        if (!envDefine(runtime->current, paramName, args[i])) {
            free(paramName);
            runtime->current = previousEnv;
            envFree(&localEnv);
            runtimeError(runtime, "Failed to bind function parameter.");
            return makeNone();
        }

        free(paramName);
    }

    result = runtimeExecuteNode(runtime, function->body);

    runtime->current = previousEnv;

    if (result.type == FLOW_RETURN) {
        returnValue = copyValue(&result.value);
        freeValue(&result.value);
        envFree(&localEnv);
        return returnValue;
    }

    if (result.type == FLOW_ERROR) {
        envFree(&localEnv);
        return makeNone();
    }

    if (result.type == FLOW_BREAK || result.type == FLOW_CONTINUE) {
        envFree(&localEnv);
        runtimeError(runtime, "break/continue used outside of loop.");
        return makeNone();
    }

    envFree(&localEnv);
    return makeNone();
}

static Value callBoundMethod(Runtime* runtime, BoundMethodObject* boundMethod, int argCount, Value* args) {
    Value* fullArgs;
    Value result;
    int i;

    if (boundMethod == NULL || boundMethod->receiver == NULL || boundMethod->method == NULL) {
        runtimeError(runtime, "Invalid bound method call.");
        return makeNone();
    }

    fullArgs = (Value*)malloc(sizeof(Value) * (size_t)(argCount + 1));
    if (fullArgs == NULL) {
        runtimeError(runtime, "Out of memory while preparing bound method call.");
        return makeNone();
    }

    fullArgs[0] = makeInstance(boundMethod->receiver);

    for (i = 0; i < argCount; i++) {
        fullArgs[i + 1] = copyValue(&args[i]);
    }

    result = callUserFunction(runtime, boundMethod->method, argCount + 1, fullArgs);

    for (i = 0; i < argCount + 1; i++) {
        freeValue(&fullArgs[i]);
    }

    free(fullArgs);
    return result;
}

static Value evalCall(Runtime* runtime, AstNode* node) {
    Value callee;
    Value result = makeNone();
    Value* args = NULL;
    int argCount = 0;
    int i;

    callee = runtimeEvalExpression(runtime, node->as.callExpr.callee);
    if (runtime->hadError) {
        return makeNone();
    }

    args = evaluateArguments(runtime, &node->as.callExpr.arguments, &argCount);
    if (runtime->hadError) {
        freeValue(&callee);
        return makeNone();
    }

    switch (callee.type) {
        case VAL_NATIVE_FUNCTION:
            if (callee.as.nativeFunction == NULL || callee.as.nativeFunction->fn == NULL) {
                runtimeError(runtime, "Invalid native function.");
            } else if (callee.as.nativeFunction->arity >= 0 &&
                       argCount != callee.as.nativeFunction->arity) {
                runtimeError(runtime, "Wrong number of arguments for native function call.");
            } else {
                result = callee.as.nativeFunction->fn(runtime, argCount, args);
            }
            break;

        case VAL_FUNCTION:
            result = callUserFunction(runtime, callee.as.function, argCount, args);
            break;

        case VAL_CLASS: {
            InstanceObject* instance;
            FunctionObject* initMethod;
            BoundMethodObject* boundInit;
            Token initName;
            Value initResult;

            instance = createInstanceObject(runtime, callee.as.classObject);
            if (runtime->hadError) {
                break;
            }

            result = makeInstance(instance);

            initName = makeSyntheticIdentifierToken("__init__");
            initMethod = classFindMethod(callee.as.classObject, initName);

            if (initMethod != NULL) {
                boundInit = createBoundMethod(runtime, instance, initMethod);
                if (runtime->hadError || boundInit == NULL) {
                    freeValue(&result);
                    result = makeNone();
                    break;
                }

                initResult = callBoundMethod(runtime, boundInit, argCount, args);

                freeValue(&initResult);
                free(boundInit);

                if (runtime->hadError) {
                    freeValue(&result);
                    result = makeNone();
                    break;
                }
            } else {
                if (argCount != 0) {
                    freeValue(&result);
                    result = makeNone();
                    runtimeError(runtime, "Constructor arguments given but no __init__ method exists.");
                    break;
                }
            }

            break;
        }

        case VAL_BOUND_METHOD:
            result = callBoundMethod(runtime, callee.as.boundMethod, argCount, args);
            break;

        default:
            runtimeError(runtime, "Tried to call a non-callable value.");
            break;
    }

    for (i = 0; i < argCount; i++) {
        freeValue(&args[i]);
    }

    free(args);
    freeValue(&callee);
    return result;
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

    if (target->type == AST_MEMBER_EXPR) {
        Value objectValue;
        char* memberName;

        objectValue = runtimeEvalExpression(runtime, target->as.memberExpr.object);
        if (runtime->hadError) {
            freeValue(&rhs);
            return execError();
        }

        if (objectValue.type != VAL_INSTANCE) {
            freeValue(&objectValue);
            freeValue(&rhs);
            runtimeError(runtime, "Only instances support member assignment right now.");
            return execError();
        }

        if (!instanceSetField(runtime, objectValue.as.instance, target->as.memberExpr.member, rhs)) {
            freeValue(&objectValue);
            freeValue(&rhs);
            return execError();
        }

        memberName = copyTokenText(target->as.memberExpr.member);
        if (memberName != NULL) {
            printf("[assign] .%s = ", memberName);
            printValue(&rhs);
            printf("\n");
            free(memberName);
        }

        freeValue(&objectValue);
        freeValue(&rhs);
        return execNormal();
    }

    freeValue(&rhs);
    runtimeError(runtime, "Only identifier or member assignment is supported right now.");
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
            return evalCall(runtime, node);

        case AST_MEMBER_EXPR: {
            Value objectValue;
            Value memberValue;
            FunctionObject* method;
            BoundMethodObject* boundMethod;

            objectValue = runtimeEvalExpression(runtime, node->as.memberExpr.object);
            if (runtime->hadError) {
                return makeNone();
            }

            if (objectValue.type != VAL_INSTANCE) {
                freeValue(&objectValue);
                runtimeError(runtime, "Only instances support member access right now.");
                return makeNone();
            }

            if (instanceGetField(runtime, objectValue.as.instance, node->as.memberExpr.member, &memberValue)) {
                freeValue(&objectValue);
                return memberValue;
            }

            runtime->hadError = 0;
            runtime->errorMessage[0] = '\0';

            method = classFindMethod(objectValue.as.instance->classObject, node->as.memberExpr.member);
            if (method != NULL) {
                boundMethod = createBoundMethod(runtime, objectValue.as.instance, method);
                freeValue(&objectValue);

                if (boundMethod == NULL) {
                    return makeNone();
                }

                return makeBoundMethod(boundMethod);
            }

            freeValue(&objectValue);
            runtimeError(runtime, "Undefined member.");
            return makeNone();
        }

        case AST_LIST_EXPR: {
            ListObject* list;
            int i;

            list = createListObject();
            if (list == NULL) {
                runtimeError(runtime, "Out of memory while creating list.");
                return makeNone();
            }

            for (i = 0; i < node->as.listExpr.elements.count; i++) {
                Value element = runtimeEvalExpression(runtime, node->as.listExpr.elements.items[i]);

                if (runtime->hadError) {
                    int j;
                    for (j = 0; j < list->count; j++) {
                        freeValue(&list->items[j]);
                    }
                    free(list->items);
                    free(list);
                    return makeNone();
                }

                if (!listAppend(list, element)) {
                    freeValue(&element);

                    {
                        int j;
                        for (j = 0; j < list->count; j++) {
                            freeValue(&list->items[j]);
                        }
                    }

                    free(list->items);
                    free(list);
                    runtimeError(runtime, "Out of memory while appending list element.");
                    return makeNone();
                }

                freeValue(&element);
            }

            return makeList(list);
        }

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

        case AST_FUNCTION_DEF: {
            FunctionObject* function;
            Value functionValue;
            char* name;

            function = createFunctionObject(runtime, node);
            if (function == NULL) {
                return execError();
            }

            functionValue = makeFunction(function);

            name = copyTokenText(node->as.functionDef.name);
            if (name == NULL) {
                runtimeError(runtime, "Out of memory while defining function.");
                return execError();
            }

            if (!envDefine(runtime->current, name, functionValue)) {
                free(name);
                runtimeError(runtime, "Failed to define function.");
                return execError();
            }

            free(name);
            return execNormal();
        }

        case AST_CLASS_DEF: {
            ClassObject* classObject;
            Value classValue;
            char* name;

            classObject = createClassObject(runtime, node);
            if (classObject == NULL) {
                return execError();
            }

            classValue = makeClass(classObject);

            name = copyTokenText(node->as.classDef.name);
            if (name == NULL) {
                runtimeError(runtime, "Out of memory while defining class.");
                return execError();
            }

            if (!envDefine(runtime->current, name, classValue)) {
                free(name);
                runtimeError(runtime, "Failed to define class.");
                return execError();
            }

            free(name);
            return execNormal();
        }

        case AST_FOR_STMT: {
            Value iterableValue;
            ExecResult forResult;

            iterableValue = runtimeEvalExpression(runtime, node->as.forStmt.iterable);
            if (runtime->hadError) {
                return execError();
            }

            forResult = executeForList(
                runtime,
                node->as.forStmt.target,
                iterableValue,
                node->as.forStmt.body
            );

            freeValue(&iterableValue);
            return forResult;
        }

        default:
            runtimeError(runtime, "Unhandled AST node in runtime.");
            return execError();
    }
}