#ifndef VALUE_H
#define VALUE_H

typedef enum {
    VAL_NONE,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STRING
} ValueType;

typedef struct {
    ValueType type;
    union {
        int boolean;
        double number;
        char* string;
    } as;
} Value;

Value makeNone(void);
Value makeBool(int boolean);
Value makeNumber(double number);
Value makeStringOwned(char* string);
Value makeStringCopy(const char* string);

void freeValue(Value* value);
Value copyValue(const Value* value);

int valueIsTruthy(const Value* value);
int valueEquals(const Value* a, const Value* b);

const char* valueTypeName(const Value* value);
void printValue(const Value* value);

#endif