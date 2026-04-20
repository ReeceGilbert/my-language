cmake_minimum_required(VERSION 3.20)

project(my_language C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(my_language
        main.c
        lexer.c
        parser.c
        ast.c
)