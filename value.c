#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* duplicateString(const char* s) {
    size_t len;
    char* copy;

    if (s == NULL) {
        return NULL;
    }

    len = strlen(s);
    copy = (char*)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, s, len + 1);
    return copy;
}

Value makeNone(void) {
    Value v;
    v.type = VAL_NONE;
    return v;
}

Value makeBool(int boolean) {
    Value v;
    v.type = VAL_BOOL;
    v.as.boolean = boolean ? 1 : 0;
    return v;
}

Value makeNumber(double number) {
    Value v;
    v.type = VAL_NUMBER;
    v.as.number = number;
    return v;
}

Value makeStringOwned(char* string) {
    Value v;
    v.type = VAL_STRING;
    v.as.string = string;
    return v;
}

Value makeStringCopy(const char* string) {
    return makeStringOwned(duplicateString(string));
}

void freeValue(Value* value) {
    if (value == NULL) {
        return;
    }

    if (value->type == VAL_STRING) {
        free(value->as.string);
        value->as.string = NULL;
    }

    value->type = VAL_NONE;
}

Value copyValue(const Value* value) {
    if (value == NULL) {
        return makeNone();
    }

    switch (value->type) {
        case VAL_NONE:
            return makeNone();
        case VAL_BOOL:
            return makeBool(value->as.boolean);
        case VAL_NUMBER:
            return makeNumber(value->as.number);
        case VAL_STRING:
            return makeStringCopy(value->as.string);
        default:
            return makeNone();
    }
}

int valueIsTruthy(const Value* value) {
    if (value == NULL) {
        return 0;
    }

    switch (value->type) {
        case VAL_NONE:
            return 0;
        case VAL_BOOL:
            return value->as.boolean != 0;
        case VAL_NUMBER:
            return value->as.number != 0.0;
        case VAL_STRING:
            return value->as.string != NULL && value->as.string[0] != '\0';
        default:
            return 0;
    }
}

int valueEquals(const Value* a, const Value* b) {
    if (a == NULL || b == NULL) {
        return 0;
    }

    if (a->type != b->type) {
        return 0;
    }

    switch (a->type) {
        case VAL_NONE:
            return 1;
        case VAL_BOOL:
            return a->as.boolean == b->as.boolean;
        case VAL_NUMBER:
            return a->as.number == b->as.number;
        case VAL_STRING:
            if (a->as.string == NULL || b->as.string == NULL) {
                return a->as.string == b->as.string;
            }
            return strcmp(a->as.string, b->as.string) == 0;
        default:
            return 0;
    }
}

const char* valueTypeName(const Value* value) {
    if (value == NULL) {
        return "null";
    }

    switch (value->type) {
        case VAL_NONE:
            return "none";
        case VAL_BOOL:
            return "bool";
        case VAL_NUMBER:
            return "number";
        case VAL_STRING:
            return "string";
        default:
            return "unknown";
    }
}

void printValue(const Value* value) {
    if (value == NULL) {
        printf("none");
        return;
    }

    switch (value->type) {
        case VAL_NONE:
            printf("none");
            break;
        case VAL_BOOL:
            printf("%s", value->as.boolean ? "true" : "false");
            break;
        case VAL_NUMBER:
            printf("%g", value->as.number);
            break;
        case VAL_STRING:
            printf("%s", value->as.string ? value->as.string : "");
            break;
        default:
            printf("<unknown>");
            break;
    }
}