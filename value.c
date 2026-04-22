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

static int ensureListCapacity(ListObject* list) {
    Value* newItems;
    int newCapacity;

    if (list == NULL) {
        return 0;
    }

    if (list->count < list->capacity) {
        return 1;
    }

    newCapacity = (list->capacity < 8) ? 8 : list->capacity * 2;
    newItems = (Value*)realloc(list->items, sizeof(Value) * (size_t)newCapacity);
    if (newItems == NULL) {
        return 0;
    }

    list->items = newItems;
    list->capacity = newCapacity;
    return 1;
}

ListObject* createListObject(void) {
    ListObject* list = (ListObject*)malloc(sizeof(ListObject));
    if (list == NULL) {
        return NULL;
    }

    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    return list;
}

int listAppend(ListObject* list, Value value) {
    if (list == NULL) {
        return 0;
    }

    if (!ensureListCapacity(list)) {
        return 0;
    }

    list->items[list->count] = copyValue(&value);
    list->count++;
    return 1;
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

Value makeNativeFunction(NativeFunction* nativeFunction) {
    Value v;
    v.type = VAL_NATIVE_FUNCTION;
    v.as.nativeFunction = nativeFunction;
    return v;
}

Value makeFunction(FunctionObject* function) {
    Value v;
    v.type = VAL_FUNCTION;
    v.as.function = function;
    return v;
}

Value makeClass(ClassObject* classObject) {
    Value v;
    v.type = VAL_CLASS;
    v.as.classObject = classObject;
    return v;
}

Value makeInstance(InstanceObject* instance) {
    Value v;
    v.type = VAL_INSTANCE;
    v.as.instance = instance;
    return v;
}

Value makeBoundMethod(BoundMethodObject* boundMethod) {
    Value v;
    v.type = VAL_BOUND_METHOD;
    v.as.boundMethod = boundMethod;
    return v;
}

Value makeList(ListObject* list) {
    Value v;
    v.type = VAL_LIST;
    v.as.list = list;
    return v;
}

void freeValue(Value* value) {
    int i;

    if (value == NULL) {
        return;
    }

    switch (value->type) {
        case VAL_STRING:
            free(value->as.string);
            value->as.string = NULL;
            break;

        case VAL_LIST:
            if (value->as.list != NULL) {
                for (i = 0; i < value->as.list->count; i++) {
                    freeValue(&value->as.list->items[i]);
                }
                free(value->as.list->items);
                free(value->as.list);
                value->as.list = NULL;
            }
            break;

        default:
            break;
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

        case VAL_NATIVE_FUNCTION:
            return makeNativeFunction(value->as.nativeFunction);

        case VAL_FUNCTION:
            return makeFunction(value->as.function);

        case VAL_CLASS:
            return makeClass(value->as.classObject);

        case VAL_INSTANCE:
            return makeInstance(value->as.instance);

        case VAL_BOUND_METHOD:
            return makeBoundMethod(value->as.boundMethod);

        case VAL_LIST: {
            ListObject* src = value->as.list;
            ListObject* dst;
            int i;

            if (src == NULL) {
                return makeList(NULL);
            }

            dst = createListObject();
            if (dst == NULL) {
                return makeNone();
            }

            for (i = 0; i < src->count; i++) {
                if (!listAppend(dst, src->items[i])) {
                    int j;
                    for (j = 0; j < dst->count; j++) {
                        freeValue(&dst->items[j]);
                    }
                    free(dst->items);
                    free(dst);
                    return makeNone();
                }
            }

            return makeList(dst);
        }

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

        case VAL_NATIVE_FUNCTION:
        case VAL_FUNCTION:
        case VAL_CLASS:
        case VAL_INSTANCE:
        case VAL_BOUND_METHOD:
            return 1;

        case VAL_LIST:
            return value->as.list != NULL && value->as.list->count > 0;

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

        case VAL_NATIVE_FUNCTION:
            return a->as.nativeFunction == b->as.nativeFunction;

        case VAL_FUNCTION:
            return a->as.function == b->as.function;

        case VAL_CLASS:
            return a->as.classObject == b->as.classObject;

        case VAL_INSTANCE:
            return a->as.instance == b->as.instance;

        case VAL_BOUND_METHOD:
            return a->as.boundMethod == b->as.boundMethod;

        case VAL_LIST:
            return a->as.list == b->as.list;

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

        case VAL_NATIVE_FUNCTION:
            return "native_function";

        case VAL_FUNCTION:
            return "function";

        case VAL_CLASS:
            return "class";

        case VAL_INSTANCE:
            return "instance";

        case VAL_BOUND_METHOD:
            return "bound_method";

        case VAL_LIST:
            return "list";

        default:
            return "unknown";
    }
}

void printValue(const Value* value) {
    int i;

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

        case VAL_NATIVE_FUNCTION:
            if (value->as.nativeFunction != NULL && value->as.nativeFunction->name != NULL) {
                printf("<native fn %s>", value->as.nativeFunction->name);
            } else {
                printf("<native fn>");
            }
            break;

        case VAL_FUNCTION:
            if (value->as.function != NULL && value->as.function->name != NULL) {
                printf("<fn %s>", value->as.function->name);
            } else {
                printf("<fn>");
            }
            break;

        case VAL_CLASS:
            if (value->as.classObject != NULL && value->as.classObject->name != NULL) {
                printf("<class %s>", value->as.classObject->name);
            } else {
                printf("<class>");
            }
            break;

        case VAL_INSTANCE:
            if (value->as.instance != NULL &&
                value->as.instance->classObject != NULL &&
                value->as.instance->classObject->name != NULL) {
                printf("<%s instance>", value->as.instance->classObject->name);
            } else {
                printf("<instance>");
            }
            break;

        case VAL_BOUND_METHOD:
            if (value->as.boundMethod != NULL &&
                value->as.boundMethod->method != NULL &&
                value->as.boundMethod->method->name != NULL) {
                printf("<bound method %s>", value->as.boundMethod->method->name);
            } else {
                printf("<bound method>");
            }
            break;

        case VAL_LIST:
            if (value->as.list == NULL) {
                printf("[]");
                break;
            }

            printf("[");
            for (i = 0; i < value->as.list->count; i++) {
                if (i > 0) {
                    printf(", ");
                }
                printValue(&value->as.list->items[i]);
            }
            printf("]");
            break;

        default:
            printf("<unknown>");
            break;
    }
}