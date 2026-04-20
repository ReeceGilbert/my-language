#include "env.h"

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

static int ensureCapacity(Environment* env) {
    Binding* newBindings;
    int newCapacity;

    if (env->count < env->capacity) {
        return 1;
    }

    newCapacity = (env->capacity < 8) ? 8 : env->capacity * 2;
    newBindings = (Binding*)realloc(env->bindings, sizeof(Binding) * newCapacity);
    if (newBindings == NULL) {
        return 0;
    }

    env->bindings = newBindings;
    env->capacity = newCapacity;
    return 1;
}

void envInit(Environment* env, Environment* parent) {
    env->parent = parent;
    env->bindings = NULL;
    env->count = 0;
    env->capacity = 0;
}

void envFree(Environment* env) {
    int i;

    if (env == NULL) {
        return;
    }

    for (i = 0; i < env->count; i++) {
        free(env->bindings[i].name);
        env->bindings[i].name = NULL;
        freeValue(&env->bindings[i].value);
    }

    free(env->bindings);
    env->bindings = NULL;
    env->count = 0;
    env->capacity = 0;
}

int envExistsInCurrent(const Environment* env, const char* name) {
    int i;

    if (env == NULL || name == NULL) {
        return 0;
    }

    for (i = 0; i < env->count; i++) {
        if (strcmp(env->bindings[i].name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

int envDefine(Environment* env, const char* name, Value value) {
    Binding* binding;

    if (env == NULL || name == NULL) {
        return 0;
    }

    if (envExistsInCurrent(env, name)) {
        return 0;
    }

    if (!ensureCapacity(env)) {
        return 0;
    }

    binding = &env->bindings[env->count++];
    binding->name = duplicateString(name);
    if (binding->name == NULL) {
        env->count--;
        return 0;
    }

    binding->value = copyValue(&value);
    return 1;
}

int envAssign(Environment* env, const char* name, Value value) {
    int i;

    if (env == NULL || name == NULL) {
        return 0;
    }

    for (i = 0; i < env->count; i++) {
        if (strcmp(env->bindings[i].name, name) == 0) {
            freeValue(&env->bindings[i].value);
            env->bindings[i].value = copyValue(&value);
            return 1;
        }
    }

    if (env->parent != NULL) {
        return envAssign(env->parent, name, value);
    }

    return 0;
}

int envGet(const Environment* env, const char* name, Value* outValue) {
    int i;

    if (env == NULL || name == NULL || outValue == NULL) {
        return 0;
    }

    for (i = 0; i < env->count; i++) {
        if (strcmp(env->bindings[i].name, name) == 0) {
            *outValue = copyValue(&env->bindings[i].value);
            return 1;
        }
    }

    if (env->parent != NULL) {
        return envGet(env->parent, name, outValue);
    }

    return 0;
}