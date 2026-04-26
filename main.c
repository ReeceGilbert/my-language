// main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "token.h"
#include "parser.h"
#include "lexer.h"
#include "runtime.h"
#include "ast.h"

static char* readEntireFile(const char* path) {
    FILE* file = fopen(path, "rb");
    long fileSize;
    char* buffer;
    size_t bytesRead;

    if (file == NULL) {
        fprintf(stderr, "Failed to open %s\n", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "Failed to seek to end of %s\n", path);
        return NULL;
    }

    fileSize = ftell(file);
    if (fileSize < 0) {
        fclose(file);
        fprintf(stderr, "Failed to determine size of %s\n", path);
        return NULL;
    }

    rewind(file);

    buffer = (char*)malloc((size_t)fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        fprintf(stderr, "Out of memory while reading %s\n", path);
        return NULL;
    }

    bytesRead = fread(buffer, 1, (size_t)fileSize, file);
    fclose(file);

    if (bytesRead != (size_t)fileSize) {
        free(buffer);
        fprintf(stderr,
                "Failed to read entire file. Read %zu of %ld bytes.\n",
                bytesRead, fileSize);
        return NULL;
    }

    buffer[fileSize] = '\0';
    return buffer;
}

typedef enum {
    MODE_RUN,
    MODE_TOKENS,
    MODE_AST,
    MODE_DEBUG
} RunMode;

static void printUsage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  nearoh <file.nr>\n"
            "  nearoh --tokens <file.nr>\n"
            "  nearoh --ast <file.nr>\n"
            "  nearoh --debug <file.nr>\n");
}

int main(int argc, char** argv) {
    const char* inputPath;
    RunMode mode = MODE_RUN;

    char* source;
    TokenArray tokens;
    Diagnostics diagnostics;
    AstNode* root;
    Runtime runtime;
    ExecResult execResult;

    if (argc < 2) {
        printUsage();
        return 1;
    }

    if (argc == 2) {
        inputPath = argv[1];
    } else if (argc == 3) {
        inputPath = argv[2];

        if (strcmp(argv[1], "--tokens") == 0) {
            mode = MODE_TOKENS;
        } else if (strcmp(argv[1], "--ast") == 0) {
            mode = MODE_AST;
        } else if (strcmp(argv[1], "--debug") == 0) {
            mode = MODE_DEBUG;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[1]);
            printUsage();
            return 1;
        }
    } else {
        printUsage();
        return 1;
    }

    initDiagnostics(&diagnostics);

    source = readEntireFile(inputPath);
    if (source == NULL) {
        return 1;
    }

    if (mode == MODE_DEBUG) {
        printf("=== SOURCE START ===\n%s\n=== SOURCE END ===\n", source);
    }

    if (!lexSource(source, &tokens, &diagnostics)) {
        printDiagnosticsSummary(&diagnostics);
        free(source);
        return 1;
    }

    normalizeTokens(&tokens);

    if (mode == MODE_TOKENS || mode == MODE_DEBUG) {
        printf("\n=== TOKENS ===\n");
        printTokenArray(&tokens);
    }

    root = parseTokens(&tokens);

    if (mode == MODE_AST || mode == MODE_DEBUG) {
        printf("\n=== AST ===\n");
        if (root != NULL) {
            printAst(root, 0);
        } else {
            printf("root is NULL\n");
        }
    }

    if (mode == MODE_DEBUG) {
        printDiagnosticsSummary(&diagnostics);
    }

    if (root != NULL && mode != MODE_TOKENS && mode != MODE_AST) {
        if (mode == MODE_DEBUG) {
            printf("\n=== EXECUTION ===\n");
        }

        runtimeInit(&runtime);
        execResult = runtimeExecuteNode(&runtime, root);

        if (runtime.hadError) {
            fprintf(stderr, "Runtime error: %s\n", runtime.errorMessage);
        }

        freeValue(&execResult.value);
        runtimeFree(&runtime);
    }

    freeAst(root);
    freeTokenArray(&tokens);
    free(source);
    return 0;
}