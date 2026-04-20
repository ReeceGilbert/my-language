// main.c
#include <stdio.h>
#include <stdlib.h>

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

int main(void) {
    const char* inputPath = "input.txt";
    char* source;
    TokenArray tokens;
    Diagnostics diagnostics;
    AstNode* root;
    Runtime runtime;
    ExecResult execResult;

    initDiagnostics(&diagnostics);

    source = readEntireFile(inputPath);
    if (source == NULL) {
        return 1;
    }

    printf("=== SOURCE START ===\n%s\n=== SOURCE END ===\n", source);

    if (!lexSource(source, &tokens, &diagnostics)) {
        printDiagnosticsSummary(&diagnostics);
        free(source);
        return 1;
    }

    normalizeTokens(&tokens);

    printf("\n=== TOKENS ===\n");
    printTokenArray(&tokens);

    printf("\n=== AST ===\n");
    root = parseTokens(&tokens);
    if (root != NULL) {
        printAst(root, 0);
    } else {
        printf("root is NULL\n");
    }

    printDiagnosticsSummary(&diagnostics);

    if (root != NULL) {
        printf("\n=== EXECUTION ===\n");

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