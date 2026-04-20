#include "builtins.h"

void registerBuiltins(Environment* globals) {
    envDefine(globals, "true", makeBool(1));
    envDefine(globals, "false", makeBool(0));
    envDefine(globals, "none", makeNone());
}