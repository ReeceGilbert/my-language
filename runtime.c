#include "runtime.h"
#include "builtins.h"
#include "env.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ImportedModule {
    char* path;
    int loaded;
    int loading;

    char* source;
    TokenArray* tokens;
    Diagnostics* diagnostics;
    AstNode* ast;
};

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

static char* copyImportPath(Token token) {
    char* text;
    int length;

    if (token.type != TOKEN_STRING || token.length < 2) {
        return NULL;
    }

    length = token.length - 2;

    text = (char*)malloc((size_t)length + 1);
    if (text == NULL) {
        return NULL;
    }

    memcpy(text, token.start + 1, (size_t)length);
    text[length] = '\0';

    return text;
}

static char* normalizeImportPath(const char* path) {
    char* normalized;
    int i;
    int out;
    int length;
    int lastWasSlash;

    if (path == NULL) {
        return NULL;
    }

    length = (int)strlen(path);
    normalized = (char*)malloc((size_t)length + 1);

    if (normalized == NULL) {
        return NULL;
    }

    i = 0;
    out = 0;
    lastWasSlash = 0;

    /*
        Strip leading "./" segments.

        "./examples/file.nr" -> "examples/file.nr"
        "././examples/file.nr" -> "examples/file.nr"
    */
    while (path[i] == '.' && (path[i + 1] == '/' || path[i + 1] == '\\')) {
        i += 2;
    }

    while (path[i] != '\0') {
        char c = path[i];

        if (c == '\\') {
            c = '/';
        }

        if (c == '/') {
            if (!lastWasSlash) {
                normalized[out++] = c;
                lastWasSlash = 1;
            }
        } else {
            normalized[out++] = c;
            lastWasSlash = 0;
        }

        i++;
    }

    normalized[out] = '\0';
    return normalized;
}

static char* readSourceFile(const char* path) {
    FILE* file;
    long size;
    char* buffer;
    size_t readCount;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char*)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    readCount = fread(buffer, 1, (size_t)size, file);
    fclose(file);

    if (readCount != (size_t)size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    return buffer;
}

static ImportedModule* findImportedModule(Runtime* runtime, const char* path) {
    int i;

    if (runtime == NULL || path == NULL) {
        return NULL;
    }

    for (i = 0; i < runtime->importCount; i++) {
        if (runtime->imports[i].path != NULL &&
            strcmp(runtime->imports[i].path, path) == 0) {
            return &runtime->imports[i];
        }
    }

    return NULL;
}

static int ensureImportCapacity(Runtime* runtime) {
    ImportedModule* newImports;
    int newCapacity;

    if (runtime == NULL) {
        return 0;
    }

    if (runtime->importCount < runtime->importCapacity) {
        return 1;
    }

    newCapacity = (runtime->importCapacity < 8)
        ? 8
        : runtime->importCapacity * 2;

    newImports = (ImportedModule*)realloc(
        runtime->imports,
        sizeof(ImportedModule) * (size_t)newCapacity
    );

    if (newImports == NULL) {
        return 0;
    }

    runtime->imports = newImports;
    runtime->importCapacity = newCapacity;
    return 1;
}

static ImportedModule* addImportedModule(Runtime* runtime, const char* path) {
    ImportedModule* module;
    char* pathCopy;

    if (runtime == NULL || path == NULL) {
        return NULL;
    }

    if (!ensureImportCapacity(runtime)) {
        return NULL;
    }

    pathCopy = (char*)malloc(strlen(path) + 1);
    if (pathCopy == NULL) {
        return NULL;
    }

    strcpy(pathCopy, path);

    module = &runtime->imports[runtime->importCount];

    module->path = pathCopy;
    module->loaded = 0;
    module->loading = 0;
    module->source = NULL;
    module->tokens = NULL;
    module->diagnostics = NULL;
    module->ast = NULL;

    runtime->importCount++;
    return module;
}

static void freeImportedModules(Runtime* runtime) {
    int i;

    if (runtime == NULL) {
        return;
    }

    for (i = 0; i < runtime->importCount; i++) {
        ImportedModule* module = &runtime->imports[i];

        free(module->path);

        if (module->tokens != NULL) {
            freeTokenArray(module->tokens);
            free(module->tokens);
        }

        /*
            AstNode trees are intentionally not freed yet because Nearoh does
            not currently expose a known AST-freeing API here, and imported
            functions/classes may have referenced imported AST nodes while the
            runtime was alive.

            This keeps the current safe behavior, but moves source/tokens into
            Runtime ownership instead of leaking them immediately.
        */

        free(module->source);
        free(module->diagnostics);
    }

    free(runtime->imports);
    runtime->imports = NULL;
    runtime->importCount = 0;
    runtime->importCapacity = 0;
}

static int ensureFileStackCapacity(Runtime* runtime) {
    char** newStack;
    int newCapacity;

    if (runtime == NULL) {
        return 0;
    }

    if (runtime->fileStackCount < runtime->fileStackCapacity) {
        return 1;
    }

    newCapacity = (runtime->fileStackCapacity < 8)
        ? 8
        : runtime->fileStackCapacity * 2;

    newStack = (char**)realloc(
        runtime->fileStack,
        sizeof(char*) * (size_t)newCapacity
    );

    if (newStack == NULL) {
        return 0;
    }

    runtime->fileStack = newStack;
    runtime->fileStackCapacity = newCapacity;
    return 1;
}

static char* copyCString(const char* text) {
    char* copy;

    if (text == NULL) {
        return NULL;
    }

    copy = (char*)malloc(strlen(text) + 1);
    if (copy == NULL) {
        return NULL;
    }

    strcpy(copy, text);
    return copy;
}

static int pushCurrentFile(Runtime* runtime, const char* path) {
    char* pathCopy;

    if (runtime == NULL || path == NULL) {
        return 0;
    }

    if (!ensureFileStackCapacity(runtime)) {
        return 0;
    }

    pathCopy = copyCString(path);
    if (pathCopy == NULL) {
        return 0;
    }

    runtime->fileStack[runtime->fileStackCount] = pathCopy;
    runtime->fileStackCount++;
    return 1;
}

static void popCurrentFile(Runtime* runtime) {
    if (runtime == NULL || runtime->fileStackCount <= 0) {
        return;
    }

    runtime->fileStackCount--;
    free(runtime->fileStack[runtime->fileStackCount]);
    runtime->fileStack[runtime->fileStackCount] = NULL;
}

static const char* getCurrentFile(Runtime* runtime) {
    if (runtime == NULL || runtime->fileStackCount <= 0) {
        return NULL;
    }

    return runtime->fileStack[runtime->fileStackCount - 1];
}

static void freeFileStack(Runtime* runtime) {
    int i;

    if (runtime == NULL) {
        return;
    }

    for (i = 0; i < runtime->fileStackCount; i++) {
        free(runtime->fileStack[i]);
    }

    free(runtime->fileStack);
    runtime->fileStack = NULL;
    runtime->fileStackCount = 0;
    runtime->fileStackCapacity = 0;
}

static int isAbsolutePath(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }

    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') {
        return 1;
    }

    return 0;
}

static char* getDirectoryName(const char* path) {
    int length;
    int i;
    char* directory;

    if (path == NULL) {
        return NULL;
    }

    length = (int)strlen(path);

    for (i = length - 1; i >= 0; i--) {
        if (path[i] == '/' || path[i] == '\\') {
            directory = (char*)malloc((size_t)i + 1);
            if (directory == NULL) {
                return NULL;
            }

            memcpy(directory, path, (size_t)i);
            directory[i] = '\0';
            return directory;
        }
    }

    return copyCString("");
}

static char* joinPaths(const char* baseDir, const char* childPath) {
    size_t baseLen;
    size_t childLen;
    int needsSlash;
    char* joined;

    if (childPath == NULL) {
        return NULL;
    }

    if (baseDir == NULL || baseDir[0] == '\0') {
        return copyCString(childPath);
    }

    baseLen = strlen(baseDir);
    childLen = strlen(childPath);
    needsSlash = baseLen > 0 &&
                 baseDir[baseLen - 1] != '/' &&
                 baseDir[baseLen - 1] != '\\';

    joined = (char*)malloc(baseLen + childLen + (needsSlash ? 2 : 1));
    if (joined == NULL) {
        return NULL;
    }

    strcpy(joined, baseDir);

    if (needsSlash) {
        joined[baseLen] = '/';
        joined[baseLen + 1] = '\0';
    }

    strcat(joined, childPath);
    return joined;
}

static char* resolveImportPath(Runtime* runtime, const char* importPath) {
    const char* currentFile;
    char* currentDir;
    char* joined;
    char* normalized;

    if (importPath == NULL) {
        return NULL;
    }

    if (isAbsolutePath(importPath)) {
        return normalizeImportPath(importPath);
    }

    currentFile = getCurrentFile(runtime);
    if (currentFile == NULL) {
        return normalizeImportPath(importPath);
    }

    currentDir = getDirectoryName(currentFile);
    if (currentDir == NULL) {
        return NULL;
    }

    joined = joinPaths(currentDir, importPath);
    free(currentDir);

    if (joined == NULL) {
        return NULL;
    }

    normalized = normalizeImportPath(joined);
    free(joined);

    return normalized;
}

static void runtimeImportReadError(Runtime* runtime, const AstNode* node, const char* path) {
    char message[256];

    if (path == NULL) {
        runtimeErrorAt(runtime, node, "Could not read imported file.");
        return;
    }

    snprintf(
        message,
        sizeof(message),
        "Could not read imported file: %s",
        path
    );

    runtimeErrorAt(runtime, node, message);
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

static Value* instanceGetFieldRef(Runtime* runtime, InstanceObject* instance, Token member) {
    char* memberName;
    int i;

    if (instance == NULL) {
        runtimeError(runtime, "Cannot get field reference from null instance.");
        return NULL;
    }

    memberName = copyTokenText(member);
    if (memberName == NULL) {
        runtimeError(runtime, "Out of memory while getting member reference.");
        return NULL;
    }

    for (i = 0; i < instance->fieldCount; i++) {
        if (strcmp(instance->fields[i].name, memberName) == 0) {
            free(memberName);
            return &instance->fields[i].value;
        }
    }

    free(memberName);
    runtimeError(runtime, "Undefined member.");
    return NULL;
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

static void runtimeNativeArityError(Runtime* runtime, NativeFunction* nativeFunction, int argCount) {
    char message[256];
    const char* name;
    int expected;

    if (nativeFunction == NULL) {
        runtimeError(runtime, "Invalid native function.");
        return;
    }

    name = nativeFunction->name == NULL ? "<native>" : nativeFunction->name;
    expected = nativeFunction->arity;

    snprintf(
        message,
        sizeof(message),
        "%s() expects exactly %d argument%s but got %d.",
        name,
        expected,
        expected == 1 ? "" : "s",
        argCount
    );

    runtimeError(runtime, message);
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
                runtimeNativeArityError(runtime, callee.as.nativeFunction, argCount);
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

static char* cookStringLiteral(Runtime* runtime, Token literal) {
    char quote;
    const char* input;
    int inputLength;
    char* output;
    int i;
    int out;

    if (literal.length < 2) {
        runtimeError(runtime, "Invalid string literal.");
        return NULL;
    }

    quote = literal.start[0];

    if ((quote != '"' && quote != '\'') ||
        literal.start[literal.length - 1] != quote) {
        runtimeError(runtime, "Invalid string literal.");
        return NULL;
    }

    input = literal.start + 1;
    inputLength = literal.length - 2;

    output = (char*)malloc((size_t)inputLength + 1);
    if (output == NULL) {
        runtimeError(runtime, "Out of memory while parsing string literal.");
        return NULL;
    }

    i = 0;
    out = 0;

    while (i < inputLength) {
        char c = input[i];

        if (c == '\\') {
            char next;

            i++;

            if (i >= inputLength) {
                free(output);
                runtimeError(runtime, "Unterminated escape sequence in string literal.");
                return NULL;
            }

            next = input[i];

            switch (next) {
                case '\\':
                    output[out++] = '\\';
                    break;

                case 'n':
                    output[out++] = '\n';
                    break;

                case 't':
                    output[out++] = '\t';
                    break;

                case 'r':
                    output[out++] = '\r';
                    break;

                case '"':
                    output[out++] = '"';
                    break;

                case '\'':
                    output[out++] = '\'';
                    break;

                default:
                    free(output);
                    runtimeError(runtime, "Unknown escape sequence in string literal.");
                    return NULL;
            }
        } else {
            output[out++] = c;
        }

        i++;
    }

    output[out] = '\0';
    return output;
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
            char* cooked = cookStringLiteral(runtime, literal);

            if (cooked == NULL) {
                return makeNone();
            }

            return makeStringOwned(cooked);
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

static Value evalIndex(Runtime* runtime, AstNode* node) {
    Value objectValue;
    Value indexValue;
    int index;

    objectValue = runtimeEvalExpression(runtime, node->as.indexExpr.object);
    if (runtime->hadError) {
        return makeNone();
    }

    indexValue = runtimeEvalExpression(runtime, node->as.indexExpr.index);
    if (runtime->hadError) {
        freeValue(&objectValue);
        return makeNone();
    }

    if (objectValue.type == VAL_DICT) {
        Value result;

        if (dictGet(objectValue.as.dict, &indexValue, &result)) {
            freeValue(&objectValue);
            freeValue(&indexValue);
            return result;
        }

        freeValue(&objectValue);
        freeValue(&indexValue);
        runtimeError(runtime, "Dictionary key not found.");
        return makeNone();
    }

    if (objectValue.type != VAL_LIST) {
        freeValue(&objectValue);
        freeValue(&indexValue);
        runtimeError(runtime, "Only lists and dictionaries support indexing right now.");
        return makeNone();
    }

    if (indexValue.type != VAL_NUMBER) {
        freeValue(&objectValue);
        freeValue(&indexValue);
        runtimeError(runtime, "List index must be a number.");
        return makeNone();
    }

    index = (int)indexValue.as.number;

    if (index < 0 || index >= objectValue.as.list->count) {
        freeValue(&objectValue);
        freeValue(&indexValue);
        runtimeError(runtime, "List index out of bounds.");
        return makeNone();
    }

    {
        Value result = copyValue(&objectValue.as.list->items[index]);
        freeValue(&objectValue);
        freeValue(&indexValue);
        return result;
    }
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
            free(memberName);
        }

        freeValue(&objectValue);
        freeValue(&rhs);
        return execNormal();
    }

    if (target->type == AST_INDEX_EXPR) {
    AstNode* objectNode = target->as.indexExpr.object;
    Value* objectRef = NULL;
    Value ownerValue;
    Value indexValue;
    int hasOwnerValue = 0;
    int index;

    if (objectNode == NULL) {
        freeValue(&rhs);
        runtimeError(runtime, "Index assignment target is null.");
        return execError();
    }

    if (objectNode->type == AST_IDENTIFIER_EXPR) {
        char* name = copyTokenText(objectNode->as.identifierExpr.name);

        if (name == NULL) {
            freeValue(&rhs);
            runtimeError(runtime, "Out of memory while assigning index.");
            return execError();
        }

        objectRef = envGetRef(runtime->current, name);
        free(name);

        if (objectRef == NULL) {
            freeValue(&rhs);
            runtimeError(runtime, "Undefined indexed variable.");
            return execError();
        }
    } else if (objectNode->type == AST_MEMBER_EXPR) {
        ownerValue = runtimeEvalExpression(runtime, objectNode->as.memberExpr.object);
        hasOwnerValue = 1;

        if (runtime->hadError) {
            freeValue(&rhs);
            return execError();
        }

        if (ownerValue.type != VAL_INSTANCE) {
            freeValue(&ownerValue);
            freeValue(&rhs);
            runtimeError(runtime, "Only instance members support member index assignment right now.");
            return execError();
        }

        objectRef = instanceGetFieldRef(
            runtime,
            ownerValue.as.instance,
            objectNode->as.memberExpr.member
        );

        if (runtime->hadError || objectRef == NULL) {
            freeValue(&ownerValue);
            freeValue(&rhs);
            return execError();
        }
    } else {
        freeValue(&rhs);
        runtimeError(runtime, "Index assignment currently requires a variable or instance member.");
        return execError();
    }

    indexValue = runtimeEvalExpression(runtime, target->as.indexExpr.index);
    if (runtime->hadError) {
        if (hasOwnerValue) {
            freeValue(&ownerValue);
        }

        freeValue(&rhs);
        return execError();
    }

    if (objectRef->type == VAL_DICT) {
        if (!dictSet(objectRef->as.dict, indexValue, rhs)) {
            freeValue(&indexValue);

            if (hasOwnerValue) {
                freeValue(&ownerValue);
            }

            freeValue(&rhs);
            runtimeError(runtime, "Failed to assign dictionary key.");
            return execError();
        }

        freeValue(&indexValue);

        if (hasOwnerValue) {
            freeValue(&ownerValue);
        }

        freeValue(&rhs);
        return execNormal();
    }

    if (objectRef->type != VAL_LIST) {
        freeValue(&indexValue);

        if (hasOwnerValue) {
            freeValue(&ownerValue);
        }

        freeValue(&rhs);
        runtimeError(runtime, "Only lists and dictionaries support index assignment right now.");
        return execError();
    }

    if (indexValue.type != VAL_NUMBER) {
        freeValue(&indexValue);

        if (hasOwnerValue) {
            freeValue(&ownerValue);
        }

        freeValue(&rhs);
        runtimeError(runtime, "List index must be a number.");
        return execError();
    }

    index = (int)indexValue.as.number;

    if (index < 0 || index > objectRef->as.list->count) {
        freeValue(&indexValue);

        if (hasOwnerValue) {
            freeValue(&ownerValue);
        }

        freeValue(&rhs);
        runtimeError(runtime, "List assignment index out of bounds.");
        return execError();
    }

    if (index == objectRef->as.list->count) {
        if (!listAppend(objectRef->as.list, rhs)) {
            freeValue(&indexValue);

            if (hasOwnerValue) {
                freeValue(&ownerValue);
            }

            freeValue(&rhs);
            runtimeError(runtime, "Out of memory while appending list item.");
            return execError();
        }

        freeValue(&indexValue);

        if (hasOwnerValue) {
            freeValue(&ownerValue);
        }

        freeValue(&rhs);
        return execNormal();
    }

    freeValue(&objectRef->as.list->items[index]);
    objectRef->as.list->items[index] = copyValue(&rhs);

    freeValue(&indexValue);

    if (hasOwnerValue) {
        freeValue(&ownerValue);
    }

    freeValue(&rhs);
    return execNormal();
}

    freeValue(&rhs);
    runtimeError(runtime, "Only identifier, member, or index assignment is supported right now.");
    return execError();
}

static ExecResult executeImportStatement(Runtime* runtime, AstNode* node) {
    char* rawPath;
    char* path;
    char* fallbackPath;
    char* source;
    ImportedModule* module;
    ExecResult result;

    if (node == NULL || node->type != AST_IMPORT_STMT) {
        runtimeError(runtime, "Invalid import statement.");
        return execError();
    }

    rawPath = copyImportPath(node->as.importStmt.path);
    if (rawPath == NULL) {
        runtimeErrorAt(runtime, node, "Import path must be a string.");
        return execError();
    }

    path = resolveImportPath(runtime, rawPath);
    if (path == NULL) {
        free(rawPath);
        runtimeError(runtime, "Out of memory while resolving import path.");
        return execError();
    }

    module = findImportedModule(runtime, path);

    if (module != NULL && module->loaded) {
        free(rawPath);
        free(path);
        return execNormal();
    }

    if (module != NULL && module->loading) {
        free(rawPath);
        free(path);
        runtimeErrorAt(runtime, node, "Circular import detected.");
        return execError();
    }

    source = readSourceFile(path);

    /*
        Compatibility fallback:

        New behavior:
            examples/modules/main.nr
            import "utils.nr"
            -> examples/modules/utils.nr

        Old examples sometimes already include "examples/...":
            examples/import_once_main.nr
            import "examples/import_once_helper.nr"
            -> first tries examples/examples/import_once_helper.nr
            -> fallback tries examples/import_once_helper.nr
    */
    if (source == NULL && !isAbsolutePath(rawPath)) {
        fallbackPath = normalizeImportPath(rawPath);

        if (fallbackPath == NULL) {
            free(rawPath);
            free(path);
            runtimeError(runtime, "Out of memory while resolving fallback import path.");
            return execError();
        }

        if (strcmp(fallbackPath, path) != 0) {
            ImportedModule* fallbackModule;

            fallbackModule = findImportedModule(runtime, fallbackPath);

            if (fallbackModule != NULL && fallbackModule->loaded) {
                free(rawPath);
                free(path);
                free(fallbackPath);
                return execNormal();
            }

            if (fallbackModule != NULL && fallbackModule->loading) {
                free(rawPath);
                free(path);
                free(fallbackPath);
                runtimeErrorAt(runtime, node, "Circular import detected.");
                return execError();
            }

            source = readSourceFile(fallbackPath);

            if (source != NULL) {
                free(path);
                path = fallbackPath;
                module = fallbackModule;
            } else {
                free(fallbackPath);
            }
        } else {
            free(fallbackPath);
        }
    }

    free(rawPath);

    if (source == NULL) {
        runtimeImportReadError(runtime, node, path);
        free(path);
        return execError();
    }

    if (module == NULL) {
        module = addImportedModule(runtime, path);
        if (module == NULL) {
            free(source);
            free(path);
            runtimeError(runtime, "Out of memory while tracking imported file.");
            return execError();
        }
    }

    module->loading = 1;
    module->source = source;

    module->tokens = (TokenArray*)malloc(sizeof(TokenArray));
    module->diagnostics = (Diagnostics*)malloc(sizeof(Diagnostics));

    if (module->tokens == NULL || module->diagnostics == NULL) {
        module->loading = 0;
        free(path);
        runtimeError(runtime, "Out of memory while importing file.");
        return execError();
    }

    initDiagnostics(module->diagnostics);

    if (!lexSource(module->source, module->tokens, module->diagnostics)) {
        module->loading = 0;
        free(path);
        runtimeErrorAt(runtime, node, "Failed to lex imported file.");
        return execError();
    }

    module->ast = parseTokens(module->tokens);
    if (source == NULL) {
        runtimeImportReadError(runtime, node, path);
        free(path);
        return execError();
    }

    annotateAstSourcePath(module->ast, module->path);

    if (!pushCurrentFile(runtime, path)) {
        module->loading = 0;
        free(path);
        runtimeError(runtime, "Out of memory while entering imported file.");
        return execError();
    }

    result = runtimeExecuteNode(runtime, module->ast);

    popCurrentFile(runtime);

    if (result.type == FLOW_NORMAL) {
        module->loaded = 1;
        module->loading = 0;
    } else {
        module->loading = 0;
    }

    free(path);
    return result;
}

void runtimeInit(Runtime* runtime) {
    envInit(&runtime->globals, NULL);
    runtime->current = &runtime->globals;
    runtime->hadError = 0;
    runtime->errorFile = NULL;
    runtime->errorLine = 0;
    runtime->errorColumn = 0;
    runtime->errorMessage[0] = '\0';

    runtime->imports = NULL;
    runtime->importCount = 0;
    runtime->importCapacity = 0;

    runtime->fileStack = NULL;
    runtime->fileStackCount = 0;
    runtime->fileStackCapacity = 0;

    registerBuiltins(&runtime->globals);
}

int runtimeSetEntryPath(Runtime* runtime, const char* path) {
    char* normalized;

    if (runtime == NULL || path == NULL) {
        return 0;
    }

    normalized = normalizeImportPath(path);
    if (normalized == NULL) {
        return 0;
    }

    if (!pushCurrentFile(runtime, normalized)) {
        free(normalized);
        return 0;
    }

    free(normalized);
    return 1;
}

void runtimeFree(Runtime* runtime) {
    envFree(&runtime->globals);
    freeImportedModules(runtime);
    freeFileStack(runtime);
    runtime->current = NULL;
}

void runtimeError(Runtime* runtime, const char* message) {
    runtime->hadError = 1;
    runtime->errorFile = NULL;
    runtime->errorLine = 0;
    runtime->errorColumn = 0;

    if (message == NULL) {
        runtime->errorMessage[0] = '\0';
        return;
    }

    strncpy(runtime->errorMessage, message, sizeof(runtime->errorMessage) - 1);
    runtime->errorMessage[sizeof(runtime->errorMessage) - 1] = '\0';
}

void runtimeErrorAt(Runtime* runtime, const AstNode* node, const char* message) {
    runtime->hadError = 1;

    if (node != NULL) {
        runtime->errorFile = node->sourcePath;
        runtime->errorLine = node->token.line;
        runtime->errorColumn = node->token.column;
    } else {
        runtime->errorFile = NULL;
        runtime->errorLine = 0;
        runtime->errorColumn = 0;
    }

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
                runtimeErrorAt(runtime, node, "Undefined variable.");
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

        case AST_INDEX_EXPR:
            return evalIndex(runtime, node);

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

        case AST_DICT_EXPR: {
            DictObject* dict;
            int i;

            dict = createDictObject();
            if (dict == NULL) {
                runtimeError(runtime, "Out of memory while creating dictionary.");
                return makeNone();
            }

            for (i = 0; i < node->as.dictExpr.keys.count; i++) {
                Value key = runtimeEvalExpression(runtime, node->as.dictExpr.keys.items[i]);
                Value value;

                if (runtime->hadError) {
                    freeValue(&key);
                    freeValue(&(Value){ .type = VAL_DICT, .as.dict = dict });
                    return makeNone();
                }

                value = runtimeEvalExpression(runtime, node->as.dictExpr.values.items[i]);

                if (runtime->hadError) {
                    freeValue(&key);
                    freeValue(&value);
                    freeValue(&(Value){ .type = VAL_DICT, .as.dict = dict });
                    return makeNone();
                }

                if (!dictSet(dict, key, value)) {
                    freeValue(&key);
                    freeValue(&value);
                    freeValue(&(Value){ .type = VAL_DICT, .as.dict = dict });
                    runtimeError(runtime, "Out of memory while adding dictionary entry.");
                    return makeNone();
                }

                freeValue(&key);
                freeValue(&value);
            }

            return makeDict(dict);
        }

        default:
            runtimeErrorAt(runtime, node, "Unsupported expression node.");
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

        case AST_IMPORT_STMT:
            return executeImportStatement(runtime, node);

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
            runtimeErrorAt(runtime, node, "Unhandled AST node in runtime.");
            return execError();
    }
}