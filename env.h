#ifndef ENV_H
#define ENV_H

#include "value.h"

typedef struct {
    char* name;
    Value value;
} Binding;

typedef struct Environment {
    struct Environment* parent;
    Binding* bindings;
    int count;
    int capacity;
} Environment;

void envInit(Environment* env, Environment* parent);
void envFree(Environment* env);

int envDefine(Environment* env, const char* name, Value value);
int envAssign(Environment* env, const char* name, Value value);
int envGet(const Environment* env, const char* name, Value* outValue);
int envExistsInCurrent(const Environment* env, const char* name);

#endif