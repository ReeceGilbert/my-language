#include "builtins.h"
#include "runtime.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Value builtinPrint(Runtime* runtime, int argCount, Value* args) {
    int i;

    (void)runtime;

    for (i = 0; i < argCount; i++) {
        if (i > 0) {
            printf(" ");
        }

        printValue(&args[i]);
    }

    printf("\n");
    return makeNone();
}

static Value builtinLen(Runtime* runtime, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(runtime, "len() expects exactly 1 argument.");
        return makeNone();
    }

    switch (args[0].type) {
        case VAL_LIST:
            return makeNumber(args[0].as.list == NULL ? 0 : (double)args[0].as.list->count);

        case VAL_DICT:
            return makeNumber(args[0].as.dict == NULL ? 0 : (double)args[0].as.dict->count);

        case VAL_STRING:
            return makeNumber(args[0].as.string == NULL ? 0 : (double)strlen(args[0].as.string));

        default:
            runtimeError(runtime, "len() expects a list, dictionary, or string.");
            return makeNone();
    }
}

static Value builtinType(Runtime* runtime, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(runtime, "type() expects exactly 1 argument.");
        return makeNone();
    }

    return makeStringCopy(valueTypeName(&args[0]));
}

static Value builtinStr(Runtime* runtime, int argCount, Value* args) {
    char buffer[128];

    if (argCount != 1) {
        runtimeError(runtime, "str() expects exactly 1 argument.");
        return makeNone();
    }

    switch (args[0].type) {
        case VAL_NONE:
            return makeStringCopy("none");

        case VAL_BOOL:
            return makeStringCopy(args[0].as.boolean ? "true" : "false");

        case VAL_NUMBER:
            snprintf(buffer, sizeof(buffer), "%g", args[0].as.number);
            return makeStringCopy(buffer);

        case VAL_STRING:
            return makeStringCopy(args[0].as.string == NULL ? "" : args[0].as.string);

        case VAL_LIST:
            return makeStringCopy("<list>");

        case VAL_DICT:
            return makeStringCopy("<dict>");

        case VAL_FUNCTION:
            return makeStringCopy("<function>");

        case VAL_NATIVE_FUNCTION:
            return makeStringCopy("<native_function>");

        case VAL_CLASS:
            return makeStringCopy("<class>");

        case VAL_INSTANCE:
            return makeStringCopy("<instance>");

        case VAL_BOUND_METHOD:
            return makeStringCopy("<bound_method>");

        default:
            return makeStringCopy("<unknown>");
    }
}

static Value builtinNum(Runtime* runtime, int argCount, Value* args) {
    char* endPtr;
    double number;

    if (argCount != 1) {
        runtimeError(runtime, "num() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type == VAL_NUMBER) {
        return makeNumber(args[0].as.number);
    }

    if (args[0].type != VAL_STRING) {
        runtimeError(runtime, "num() expects a string or number.");
        return makeNone();
    }

    if (args[0].as.string == NULL) {
        runtimeError(runtime, "num() cannot convert a null string.");
        return makeNone();
    }

    number = strtod(args[0].as.string, &endPtr);

    if (endPtr == args[0].as.string) {
        runtimeError(runtime, "num() could not parse number.");
        return makeNone();
    }

    while (*endPtr == ' ' || *endPtr == '\t' || *endPtr == '\r' || *endPtr == '\n') {
        endPtr++;
    }

    if (*endPtr != '\0') {
        runtimeError(runtime, "num() string contains non-number characters.");
        return makeNone();
    }

    return makeNumber(number);
}

static Value builtinAppend(Runtime* runtime, int argCount, Value* args) {
    ListObject* result;
    int i;

    if (argCount != 2) {
        runtimeError(runtime, "append() expects exactly 2 arguments.");
        return makeNone();
    }

    if (args[0].type != VAL_LIST) {
        runtimeError(runtime, "append() expects the first argument to be a list.");
        return makeNone();
    }

    if (args[0].as.list == NULL) {
        runtimeError(runtime, "append() cannot append to a null list.");
        return makeNone();
    }

    result = createListObject();
    if (result == NULL) {
        runtimeError(runtime, "Out of memory while creating appended list.");
        return makeNone();
    }

    for (i = 0; i < args[0].as.list->count; i++) {
        if (!listAppend(result, args[0].as.list->items[i])) {
            freeValue(&(Value){ .type = VAL_LIST, .as.list = result });
            runtimeError(runtime, "Out of memory while copying list for append().");
            return makeNone();
        }
    }

    if (!listAppend(result, args[1])) {
        freeValue(&(Value){ .type = VAL_LIST, .as.list = result });
        runtimeError(runtime, "Out of memory while appending list item.");
        return makeNone();
    }

    return makeList(result);
}

static Value builtinRange(Runtime* runtime, int argCount, Value* args) {
    ListObject* result;
    int count;
    int i;

    if (argCount != 1) {
        runtimeError(runtime, "range() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_NUMBER) {
        runtimeError(runtime, "range() expects a number.");
        return makeNone();
    }

    count = (int)args[0].as.number;

    if (count < 0) {
        runtimeError(runtime, "range() count cannot be negative.");
        return makeNone();
    }

    result = createListObject();
    if (result == NULL) {
        runtimeError(runtime, "Out of memory while creating range list.");
        return makeNone();
    }

    for (i = 0; i < count; i++) {
        Value number = makeNumber((double)i);

        if (!listAppend(result, number)) {
            freeValue(&(Value){ .type = VAL_LIST, .as.list = result });
            runtimeError(runtime, "Out of memory while building range list.");
            return makeNone();
        }
    }

    return makeList(result);
}

static Value builtinKeys(Runtime* runtime, int argCount, Value* args) {
    ListObject* result;
    int i;

    if (argCount != 1) {
        runtimeError(runtime, "keys() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_DICT) {
        runtimeError(runtime, "keys() expects a dictionary.");
        return makeNone();
    }

    if (args[0].as.dict == NULL) {
        runtimeError(runtime, "keys() cannot read a null dictionary.");
        return makeNone();
    }

    result = createListObject();
    if (result == NULL) {
        runtimeError(runtime, "Out of memory while creating keys list.");
        return makeNone();
    }

    for (i = 0; i < args[0].as.dict->count; i++) {
        if (!listAppend(result, args[0].as.dict->entries[i].key)) {
            freeValue(&(Value){ .type = VAL_LIST, .as.list = result });
            runtimeError(runtime, "Out of memory while building keys list.");
            return makeNone();
        }
    }

    return makeList(result);
}

static Value builtinHas(Runtime* runtime, int argCount, Value* args) {
    Value found;

    if (argCount != 2) {
        runtimeError(runtime, "has() expects exactly 2 arguments.");
        return makeNone();
    }

    if (args[0].type != VAL_DICT) {
        runtimeError(runtime, "has() expects the first argument to be a dictionary.");
        return makeNone();
    }

    if (args[0].as.dict == NULL) {
        return makeBool(0);
    }

    if (dictGet(args[0].as.dict, &args[1], &found)) {
        freeValue(&found);
        return makeBool(1);
    }

    return makeBool(0);
}

static Value builtinReadFile(Runtime* runtime, int argCount, Value* args) {
    FILE* file;
    long size;
    char* buffer;
    size_t readCount;

    if (argCount != 1) {
        runtimeError(runtime, "read_file() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_STRING || args[0].as.string == NULL) {
        runtimeError(runtime, "read_file() expects a file path string.");
        return makeNone();
    }

    file = fopen(args[0].as.string, "rb");
    if (file == NULL) {
        runtimeError(runtime, "read_file() could not open file.");
        return makeNone();
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        runtimeError(runtime, "read_file() failed to seek file.");
        return makeNone();
    }

    size = ftell(file);
    if (size < 0) {
        fclose(file);
        runtimeError(runtime, "read_file() failed to get file size.");
        return makeNone();
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        runtimeError(runtime, "read_file() failed to rewind file.");
        return makeNone();
    }

    buffer = (char*)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        runtimeError(runtime, "Out of memory while reading file.");
        return makeNone();
    }

    readCount = fread(buffer, 1, (size_t)size, file);
    fclose(file);

    if (readCount != (size_t)size) {
        free(buffer);
        runtimeError(runtime, "read_file() failed to read entire file.");
        return makeNone();
    }

    buffer[size] = '\0';
    return makeStringOwned(buffer);
}

static Value builtinWriteFile(Runtime* runtime, int argCount, Value* args) {
    FILE* file;
    const char* path;
    const char* text;
    size_t length;
    size_t written;

    if (argCount != 2) {
        runtimeError(runtime, "write_file() expects exactly 2 arguments.");
        return makeNone();
    }

    if (args[0].type != VAL_STRING || args[0].as.string == NULL) {
        runtimeError(runtime, "write_file() expects the first argument to be a file path string.");
        return makeNone();
    }

    if (args[1].type != VAL_STRING || args[1].as.string == NULL) {
        runtimeError(runtime, "write_file() expects the second argument to be a string.");
        return makeNone();
    }

    path = args[0].as.string;
    text = args[1].as.string;

    file = fopen(path, "wb");
    if (file == NULL) {
        runtimeError(runtime, "write_file() could not open file for writing.");
        return makeNone();
    }

    length = strlen(text);
    written = fwrite(text, 1, length, file);
    fclose(file);

    if (written != length) {
        runtimeError(runtime, "write_file() failed to write entire file.");
        return makeNone();
    }

    return makeBool(1);
}

static Value builtinAppendFile(Runtime* runtime, int argCount, Value* args) {
    FILE* file;
    const char* path;
    const char* text;
    size_t length;
    size_t written;

    if (argCount != 2) {
        runtimeError(runtime, "append_file() expects exactly 2 arguments.");
        return makeNone();
    }

    if (args[0].type != VAL_STRING || args[0].as.string == NULL) {
        runtimeError(runtime, "append_file() expects the first argument to be a file path string.");
        return makeNone();
    }

    if (args[1].type != VAL_STRING || args[1].as.string == NULL) {
        runtimeError(runtime, "append_file() expects the second argument to be a string.");
        return makeNone();
    }

    path = args[0].as.string;
    text = args[1].as.string;

    file = fopen(path, "ab");
    if (file == NULL) {
        runtimeError(runtime, "append_file() could not open file for appending.");
        return makeNone();
    }

    length = strlen(text);
    written = fwrite(text, 1, length, file);
    fclose(file);

    if (written != length) {
        runtimeError(runtime, "append_file() failed to write entire file.");
        return makeNone();
    }

    return makeBool(1);
}

static Value builtinAbs(Runtime* runtime, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(runtime, "abs() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_NUMBER) {
        runtimeError(runtime, "abs() expects a number.");
        return makeNone();
    }

    return makeNumber(args[0].as.number < 0 ? -args[0].as.number : args[0].as.number);
}

static Value builtinFloor(Runtime* runtime, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(runtime, "floor() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_NUMBER) {
        runtimeError(runtime, "floor() expects a number.");
        return makeNone();
    }

    return makeNumber(floor(args[0].as.number));
}

static Value builtinCeil(Runtime* runtime, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(runtime, "ceil() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_NUMBER) {
        runtimeError(runtime, "ceil() expects a number.");
        return makeNone();
    }

    return makeNumber(ceil(args[0].as.number));
}

static Value builtinMin(Runtime* runtime, int argCount, Value* args) {
    double result;
    int i;

    if (argCount < 1) {
        runtimeError(runtime, "min() expects at least 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_NUMBER) {
        runtimeError(runtime, "min() expects only numbers.");
        return makeNone();
    }

    result = args[0].as.number;

    for (i = 1; i < argCount; i++) {
        if (args[i].type != VAL_NUMBER) {
            runtimeError(runtime, "min() expects only numbers.");
            return makeNone();
        }

        if (args[i].as.number < result) {
            result = args[i].as.number;
        }
    }

    return makeNumber(result);
}

static Value builtinMax(Runtime* runtime, int argCount, Value* args) {
    double result;
    int i;

    if (argCount < 1) {
        runtimeError(runtime, "max() expects at least 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_NUMBER) {
        runtimeError(runtime, "max() expects only numbers.");
        return makeNone();
    }

    result = args[0].as.number;

    for (i = 1; i < argCount; i++) {
        if (args[i].type != VAL_NUMBER) {
            runtimeError(runtime, "max() expects only numbers.");
            return makeNone();
        }

        if (args[i].as.number > result) {
            result = args[i].as.number;
        }
    }

    return makeNumber(result);
}

static Value builtinClockMs(Runtime* runtime, int argCount, Value* args) {
    double ms;

    (void)args;

    if (argCount != 0) {
        runtimeError(runtime, "clock_ms() expects exactly 0 arguments.");
        return makeNone();
    }

    ms = ((double)clock() * 1000.0) / (double)CLOCKS_PER_SEC;
    return makeNumber(ms);
}

static void ensureRandomSeeded(void) {
    static int seeded = 0;

    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
}

static Value builtinSeedRandom(Runtime* runtime, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(runtime, "seed_random() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_NUMBER) {
        runtimeError(runtime, "seed_random() expects a number.");
        return makeNone();
    }

    srand((unsigned int)args[0].as.number);
    return makeBool(1);
}

static Value builtinRandom(Runtime* runtime, int argCount, Value* args) {
    double value;

    (void)args;

    if (argCount != 0) {
        runtimeError(runtime, "random() expects exactly 0 arguments.");
        return makeNone();
    }

    ensureRandomSeeded();

    value = (double)rand() / ((double)RAND_MAX + 1.0);
    return makeNumber(value);
}

static Value builtinRandInt(Runtime* runtime, int argCount, Value* args) {
    int max;
    int value;

    if (argCount != 1) {
        runtimeError(runtime, "randint() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_NUMBER) {
        runtimeError(runtime, "randint() expects a number.");
        return makeNone();
    }

    max = (int)args[0].as.number;

    if (max <= 0) {
        runtimeError(runtime, "randint() max must be greater than 0.");
        return makeNone();
    }

    ensureRandomSeeded();

    value = rand() % max;
    return makeNumber((double)value);
}

static Value builtinValues(Runtime* runtime, int argCount, Value* args) {
    ListObject* result;
    int i;

    if (argCount != 1) {
        runtimeError(runtime, "values() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_DICT) {
        runtimeError(runtime, "values() expects a dictionary.");
        return makeNone();
    }

    if (args[0].as.dict == NULL) {
        runtimeError(runtime, "values() cannot read a null dictionary.");
        return makeNone();
    }

    result = createListObject();
    if (result == NULL) {
        runtimeError(runtime, "Out of memory while creating values list.");
        return makeNone();
    }

    for (i = 0; i < args[0].as.dict->count; i++) {
        if (!listAppend(result, args[0].as.dict->entries[i].value)) {
            freeValue(&(Value){ .type = VAL_LIST, .as.list = result });
            runtimeError(runtime, "Out of memory while building values list.");
            return makeNone();
        }
    }

    return makeList(result);
}

static Value builtinLower(Runtime* runtime, int argCount, Value* args) {
    char* result;
    int i;
    int length;

    if (argCount != 1) {
        runtimeError(runtime, "lower() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_STRING || args[0].as.string == NULL) {
        runtimeError(runtime, "lower() expects a string.");
        return makeNone();
    }

    length = (int)strlen(args[0].as.string);
    result = (char*)malloc((size_t)length + 1);

    if (result == NULL) {
        runtimeError(runtime, "Out of memory while creating lowercase string.");
        return makeNone();
    }

    for (i = 0; i < length; i++) {
        result[i] = (char)tolower((unsigned char)args[0].as.string[i]);
    }

    result[length] = '\0';
    return makeStringOwned(result);
}

static Value builtinUpper(Runtime* runtime, int argCount, Value* args) {
    char* result;
    int i;
    int length;

    if (argCount != 1) {
        runtimeError(runtime, "upper() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_STRING || args[0].as.string == NULL) {
        runtimeError(runtime, "upper() expects a string.");
        return makeNone();
    }

    length = (int)strlen(args[0].as.string);
    result = (char*)malloc((size_t)length + 1);

    if (result == NULL) {
        runtimeError(runtime, "Out of memory while creating uppercase string.");
        return makeNone();
    }

    for (i = 0; i < length; i++) {
        result[i] = (char)toupper((unsigned char)args[0].as.string[i]);
    }

    result[length] = '\0';
    return makeStringOwned(result);
}

static Value builtinTrim(Runtime* runtime, int argCount, Value* args) {
    const char* text;
    const char* start;
    const char* end;
    char* result;
    size_t length;

    if (argCount != 1) {
        runtimeError(runtime, "trim() expects exactly 1 argument.");
        return makeNone();
    }

    if (args[0].type != VAL_STRING || args[0].as.string == NULL) {
        runtimeError(runtime, "trim() expects a string.");
        return makeNone();
    }

    text = args[0].as.string;
    start = text;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    end = start + strlen(start);

    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    length = (size_t)(end - start);
    result = (char*)malloc(length + 1);

    if (result == NULL) {
        runtimeError(runtime, "Out of memory while trimming string.");
        return makeNone();
    }

    memcpy(result, start, length);
    result[length] = '\0';

    return makeStringOwned(result);
}

static Value builtinContains(Runtime* runtime, int argCount, Value* args) {
    if (argCount != 2) {
        runtimeError(runtime, "contains() expects exactly 2 arguments.");
        return makeNone();
    }

    if (args[0].type != VAL_STRING || args[0].as.string == NULL) {
        runtimeError(runtime, "contains() expects the first argument to be a string.");
        return makeNone();
    }

    if (args[1].type != VAL_STRING || args[1].as.string == NULL) {
        runtimeError(runtime, "contains() expects the second argument to be a string.");
        return makeNone();
    }

    return makeBool(strstr(args[0].as.string, args[1].as.string) != NULL);
}

static Value builtinSubstr(Runtime* runtime, int argCount, Value* args) {
    const char* text;
    char* result;
    int textLength;
    int start;
    int length;

    if (argCount != 3) {
        runtimeError(runtime, "substr() expects exactly 3 arguments.");
        return makeNone();
    }

    if (args[0].type != VAL_STRING || args[0].as.string == NULL) {
        runtimeError(runtime, "substr() expects the first argument to be a string.");
        return makeNone();
    }

    if (args[1].type != VAL_NUMBER) {
        runtimeError(runtime, "substr() expects the second argument to be a number.");
        return makeNone();
    }

    if (args[2].type != VAL_NUMBER) {
        runtimeError(runtime, "substr() expects the third argument to be a number.");
        return makeNone();
    }

    text = args[0].as.string;
    textLength = (int)strlen(text);
    start = (int)args[1].as.number;
    length = (int)args[2].as.number;

    if (start < 0 || start > textLength) {
        runtimeError(runtime, "substr() start index out of bounds.");
        return makeNone();
    }

    if (length < 0) {
        runtimeError(runtime, "substr() length cannot be negative.");
        return makeNone();
    }

    if (start + length > textLength) {
        length = textLength - start;
    }

    result = (char*)malloc((size_t)length + 1);

    if (result == NULL) {
        runtimeError(runtime, "Out of memory while creating substring.");
        return makeNone();
    }

    memcpy(result, text + start, (size_t)length);
    result[length] = '\0';

    return makeStringOwned(result);
}

void registerBuiltins(Environment* globals) {
    static NativeFunction printFunction = {
        "print",
        -1,
        builtinPrint
    };

    static NativeFunction lenFunction = {
        "len",
        1,
        builtinLen
    };

    static NativeFunction typeFunction = {
        "type",
        1,
        builtinType
    };

    static NativeFunction strFunction = {
        "str",
        1,
        builtinStr
    };

    static NativeFunction numFunction = {
        "num",
        1,
        builtinNum
    };

    static NativeFunction appendFunction = {
        "append",
        2,
        builtinAppend
    };

    static NativeFunction rangeFunction = {
        "range",
        1,
        builtinRange
    };

    static NativeFunction keysFunction = {
        "keys",
        1,
        builtinKeys
    };

    static NativeFunction hasFunction = {
        "has",
        2,
        builtinHas
    };

    static NativeFunction readFileFunction = {
        "read_file",
        1,
        builtinReadFile
    };

    static NativeFunction writeFileFunction = {
        "write_file",
        2,
        builtinWriteFile
    };

    static NativeFunction appendFileFunction = {
        "append_file",
        2,
        builtinAppendFile
    };

    static NativeFunction absFunction = {
        "abs",
        1,
        builtinAbs
    };

    static NativeFunction floorFunction = {
        "floor",
        1,
        builtinFloor
    };

    static NativeFunction ceilFunction = {
        "ceil",
        1,
        builtinCeil
    };

    static NativeFunction minFunction = {
        "min",
        -1,
        builtinMin
    };

    static NativeFunction maxFunction = {
        "max",
        -1,
        builtinMax
    };

    static NativeFunction clockMsFunction = {
        "clock_ms",
        0,
        builtinClockMs
    };

    static NativeFunction randomFunction = {
        "random",
        0,
        builtinRandom
    };

    static NativeFunction seedRandomFunction = {
        "seed_random",
        1,
        builtinSeedRandom
    };

    static NativeFunction randIntFunction = {
        "randint",
        1,
        builtinRandInt
    };

    static NativeFunction valuesFunction = {
        "values",
        1,
        builtinValues
    };

    static NativeFunction lowerFunction = {
        "lower",
        1,
        builtinLower
    };

    static NativeFunction upperFunction = {
        "upper",
        1,
        builtinUpper
    };

    static NativeFunction trimFunction = {
        "trim",
        1,
        builtinTrim
    };

    static NativeFunction containsFunction = {
        "contains",
        2,
        builtinContains
    };

    static NativeFunction substrFunction = {
        "substr",
        3,
        builtinSubstr
    };

    envDefine(globals, "true", makeBool(1));
    envDefine(globals, "false", makeBool(0));
    envDefine(globals, "none", makeNone());

    envDefine(globals, "print", makeNativeFunction(&printFunction));
    envDefine(globals, "len", makeNativeFunction(&lenFunction));
    envDefine(globals, "type", makeNativeFunction(&typeFunction));
    envDefine(globals, "str", makeNativeFunction(&strFunction));
    envDefine(globals, "num", makeNativeFunction(&numFunction));
    envDefine(globals, "append", makeNativeFunction(&appendFunction));
    envDefine(globals, "range", makeNativeFunction(&rangeFunction));
    envDefine(globals, "keys", makeNativeFunction(&keysFunction));
    envDefine(globals, "has", makeNativeFunction(&hasFunction));
    envDefine(globals, "read_file", makeNativeFunction(&readFileFunction));
    envDefine(globals, "write_file", makeNativeFunction(&writeFileFunction));
    envDefine(globals, "append_file", makeNativeFunction(&appendFileFunction));
    envDefine(globals, "abs", makeNativeFunction(&absFunction));
    envDefine(globals, "floor", makeNativeFunction(&floorFunction));
    envDefine(globals, "ceil", makeNativeFunction(&ceilFunction));
    envDefine(globals, "min", makeNativeFunction(&minFunction));
    envDefine(globals, "max", makeNativeFunction(&maxFunction));
    envDefine(globals, "clock_ms", makeNativeFunction(&clockMsFunction));
    envDefine(globals, "random", makeNativeFunction(&randomFunction));
    envDefine(globals, "seed_random", makeNativeFunction(&seedRandomFunction));
    envDefine(globals, "randint", makeNativeFunction(&randIntFunction));
    envDefine(globals, "values", makeNativeFunction(&valuesFunction));
    envDefine(globals, "lower", makeNativeFunction(&lowerFunction));
    envDefine(globals, "upper", makeNativeFunction(&upperFunction));
    envDefine(globals, "trim", makeNativeFunction(&trimFunction));
    envDefine(globals, "contains", makeNativeFunction(&containsFunction));
    envDefine(globals, "substr", makeNativeFunction(&substrFunction));
}