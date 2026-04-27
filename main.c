// main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "runtime.h"
#include "token.h"

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

static int parseArguments(int argc, char** argv, RunMode* mode, const char** inputPath) {
    *mode = MODE_RUN;
    *inputPath = NULL;

    if (argc == 2) {
        *inputPath = argv[1];
        return 1;
    }

    if (argc == 3) {
        *inputPath = argv[2];

        if (strcmp(argv[1], "--tokens") == 0) {
            *mode = MODE_TOKENS;
            return 1;
        }

        if (strcmp(argv[1], "--ast") == 0) {
            *mode = MODE_AST;
            return 1;
        }

        if (strcmp(argv[1], "--debug") == 0) {
            *mode = MODE_DEBUG;
            return 1;
        }

        fprintf(stderr, "Unknown option: %s\n", argv[1]);
        return 0;
    }

    return 0;
}

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
        fprintf(stderr, "Failed to seek to end of %s\n", path);
        fclose(file);
        return NULL;
    }

    fileSize = ftell(file);
    if (fileSize < 0) {
        fprintf(stderr, "Failed to determine size of %s\n", path);
        fclose(file);
        return NULL;
    }

    rewind(file);

    buffer = (char*)malloc((size_t)fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Out of memory while reading %s\n", path);
        fclose(file);
        return NULL;
    }

    bytesRead = fread(buffer, 1, (size_t)fileSize, file);
    fclose(file);

    if (bytesRead != (size_t)fileSize) {
        fprintf(stderr,
                "Failed to read entire file. Read %zu of %ld bytes.\n",
                bytesRead,
                fileSize);
        free(buffer);
        return NULL;
    }

    buffer[fileSize] = '\0';
    return buffer;
}

int main(int argc, char** argv) {
    const char* inputPath = NULL;
    RunMode mode = MODE_RUN;

    char* source = NULL;
    TokenArray tokens;
    Diagnostics diagnostics;
    AstNode* root = NULL;
    Runtime runtime;
    ExecResult execResult;

    int tokensInitialized = 0;
    int runtimeInitialized = 0;
    int exitCode = 0;

    if (!parseArguments(argc, argv, &mode, &inputPath)) {
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
        exitCode = 1;
        goto cleanup;
    }

    tokensInitialized = 1;
    normalizeTokens(&tokens);

    if (mode == MODE_TOKENS || mode == MODE_DEBUG) {
        printf("\n=== TOKENS ===\n");
        printTokenArray(&tokens);
    }

    if (mode == MODE_TOKENS) {
        goto cleanup;
    }

    root = parseTokens(&tokens);

    if (root == NULL) {
        fprintf(stderr, "Parse failed: AST root is NULL.\n");
        exitCode = 1;
        goto cleanup;
    }

    if (mode == MODE_AST || mode == MODE_DEBUG) {
        printf("\n=== AST ===\n");
        printAst(root, 0);
    }

    if (mode == MODE_AST) {
        goto cleanup;
    }

    if (mode == MODE_DEBUG) {
        printDiagnosticsSummary(&diagnostics);
        printf("\n=== EXECUTION ===\n");
    }

    runtimeInit(&runtime);
    runtimeInitialized = 1;

    execResult = runtimeExecuteNode(&runtime, root);

    if (runtime.hadError) {
        if (runtime.errorLine > 0) {
            fprintf(stderr,
                    "Runtime error at line %d col %d: %s\n",
                    runtime.errorLine,
                    runtime.errorColumn,
                    runtime.errorMessage);
        } else {
            fprintf(stderr, "Runtime error: %s\n", runtime.errorMessage);
        }
        exitCode = 1;
    }

    freeValue(&execResult.value);

cleanup:
    if (runtimeInitialized) {
        runtimeFree(&runtime);
    }

    freeAst(root);

    if (tokensInitialized) {
        freeTokenArray(&tokens);
    }

    free(source);
    return exitCode;
}