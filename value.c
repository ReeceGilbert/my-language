#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIST_INITIAL_CAPACITY 8

static char* duplicateString(const char* text);
static int ensureListCapacity(ListObject* list);
static void freeListObject(ListObject* list);

// ------------------------------------------------------------
// INTERNAL HELPERS
// ------------------------------------------------------------

static char* duplicateString(const char* text) {
    size_t length;
    char* copy;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text);
    copy = (char*)malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, length + 1);
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

    newCapacity = (list->capacity < LIST_INITIAL_CAPACITY)
        ? LIST_INITIAL_CAPACITY
        : list->capacity * 2;

    newItems = (Value*)realloc(list->items, sizeof(Value) * (size_t)newCapacity);

    if (newItems == NULL) {
        return 0;
    }

    list->items = newItems;
    list->capacity = newCapacity;
    return 1;
}

static void freeListObject(ListObject* list) {
    int i;

    if (list == NULL) {
        return;
    }

    for (i = 0; i < list->count; i++) {
        freeValue(&list->items[i]);
    }

    free(list->items);
    free(list);
}

// ------------------------------------------------------------
// LISTS
// ------------------------------------------------------------

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

// ------------------------------------------------------------
// VALUE CONSTRUCTORS
// ------------------------------------------------------------

Value makeNone(void) {
    Value value;

    value.type = VAL_NONE;

    return value;
}

Value makeBool(int boolean) {
    Value value;

    value.type = VAL_BOOL;
    value.as.boolean = boolean ? 1 : 0;

    return value;
}

Value makeNumber(double number) {
    Value value;

    value.type = VAL_NUMBER;
    value.as.number = number;

    return value;
}

Value makeStringOwned(char* string) {
    Value value;

    value.type = VAL_STRING;
    value.as.string = string;

    return value;
}

Value makeStringCopy(const char* string) {
    return makeStringOwned(duplicateString(string));
}

Value makeNativeFunction(NativeFunction* nativeFunction) {
    Value value;

    value.type = VAL_NATIVE_FUNCTION;
    value.as.nativeFunction = nativeFunction;

    return value;
}

Value makeFunction(FunctionObject* function) {
    Value value;

    value.type = VAL_FUNCTION;
    value.as.function = function;

    return value;
}

Value makeClass(ClassObject* classObject) {
    Value value;

    value.type = VAL_CLASS;
    value.as.classObject = classObject;

    return value;
}

Value makeInstance(InstanceObject* instance) {
    Value value;

    value.type = VAL_INSTANCE;
    value.as.instance = instance;

    return value;
}

Value makeBoundMethod(BoundMethodObject* boundMethod) {
    Value value;

    value.type = VAL_BOUND_METHOD;
    value.as.boundMethod = boundMethod;

    return value;
}

Value makeList(ListObject* list) {
    Value value;

    value.type = VAL_LIST;
    value.as.list = list;

    return value;
}

// ------------------------------------------------------------
// VALUE LIFETIME
// ------------------------------------------------------------

void freeValue(Value* value) {
    if (value == NULL) {
        return;
    }

    switch (value->type) {
        case VAL_STRING:
            free(value->as.string);
            value->as.string = NULL;
            break;

        case VAL_LIST:
            freeListObject(value->as.list);
            value->as.list = NULL;
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
            ListObject* source = value->as.list;
            ListObject* copy;
            int i;

            if (source == NULL) {
                return makeList(NULL);
            }

            copy = createListObject();

            if (copy == NULL) {
                return makeNone();
            }

            for (i = 0; i < source->count; i++) {
                if (!listAppend(copy, source->items[i])) {
                    freeListObject(copy);
                    return makeNone();
                }
            }

            return makeList(copy);
        }

        default:
            return makeNone();
    }
}

// ------------------------------------------------------------
// VALUE BEHAVIOR
// ------------------------------------------------------------

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

        case VAL_LIST:
            return value->as.list != NULL && value->as.list->count > 0;

        case VAL_NATIVE_FUNCTION:
        case VAL_FUNCTION:
        case VAL_CLASS:
        case VAL_INSTANCE:
        case VAL_BOUND_METHOD:
            return 1;

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
            if (value->as.nativeFunction != NULL &&
                value->as.nativeFunction->name != NULL) {
                printf("<native fn %s>", value->as.nativeFunction->name);
            } else {
                printf("<native fn>");
            }
            break;

        case VAL_FUNCTION:
            if (value->as.function != NULL &&
                value->as.function->name != NULL) {
                printf("<fn %s>", value->as.function->name);
            } else {
                printf("<fn>");
            }
            break;

        case VAL_CLASS:
            if (value->as.classObject != NULL &&
                value->as.classObject->name != NULL) {
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