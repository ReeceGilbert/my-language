cmake_minimum_required(VERSION 3.31)
project(nearoh C)

set(CMAKE_C_STANDARD 11)

set(SOURCES
        main.c
        lexer.c
        parser.c
        ast.c
        runtime.c
        value.c
        env.c
        builtins.c
)

set(HEADERS
        lexer.h
        parser.h
        ast.h
        runtime.h
        value.h
        env.h
        builtins.h
        token.h
)

add_executable(nearoh
        ${SOURCES}
        ${HEADERS}
)