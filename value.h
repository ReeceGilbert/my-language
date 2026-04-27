#ifndef VALUE_H
#define VALUE_H

#include "token.h"

typedef struct AstNode AstNode;
typedef struct Environment Environment;
typedef struct Runtime Runtime;

typedef struct Value Value;
typedef struct NativeFunction NativeFunction;

typedef struct FunctionObject FunctionObject;
typedef struct MethodEntry MethodEntry;
typedef struct ClassObject ClassObject;

typedef struct InstanceField InstanceField;
typedef struct InstanceObject InstanceObject;
typedef struct BoundMethodObject BoundMethodObject;

typedef struct ListObject ListObject;

typedef enum {
    VAL_NONE,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STRING,
    VAL_NATIVE_FUNCTION,
    VAL_FUNCTION,
    VAL_CLASS,
    VAL_INSTANCE,
    VAL_BOUND_METHOD,
    VAL_LIST
} ValueType;

struct FunctionObject {
    char* name;
    Token* params;
    int paramCount;
    AstNode* body;
    Environment* closure;
};

struct MethodEntry {
    char* name;
    FunctionObject* function;
};

struct ClassObject {
    char* name;
    AstNode* body;
    Environment* closure;
    MethodEntry* methods;
    int methodCount;
    int methodCapacity;
};

struct BoundMethodObject {
    InstanceObject* receiver;
    FunctionObject* method;
};

struct ListObject {
    Value* items;
    int count;
    int capacity;
};

struct Value {
    ValueType type;

    union {
        int boolean;
        double number;
        char* string;
        NativeFunction* nativeFunction;
        FunctionObject* function;
        ClassObject* classObject;
        InstanceObject* instance;
        BoundMethodObject* boundMethod;
        ListObject* list;
    } as;
};

struct NativeFunction {
    const char* name;
    int arity;
    Value (*fn)(Runtime* runtime, int argCount, Value* args);
};

struct InstanceField {
    char* name;
    Value value;
};

struct InstanceObject {
    ClassObject* classObject;
    InstanceField* fields;
    int fieldCount;
    int fieldCapacity;
};

Value makeNone(void);
Value makeBool(int boolean);
Value makeNumber(double number);

Value makeStringOwned(char* string);
Value makeStringCopy(const char* string);

Value makeNativeFunction(NativeFunction* nativeFunction);
Value makeFunction(FunctionObject* function);
Value makeClass(ClassObject* classObject);
Value makeInstance(InstanceObject* instance);
Value makeBoundMethod(BoundMethodObject* boundMethod);

Value makeList(ListObject* list);
ListObject* createListObject(void);
int listAppend(ListObject* list, Value value);

void freeValue(Value* value);
Value copyValue(const Value* value);

int valueIsTruthy(const Value* value);
int valueEquals(const Value* a, const Value* b);

const char* valueTypeName(const Value* value);
void printValue(const Value* value);

#endif