cmake_minimum_required(VERSION 4.1)
project(untitled3 C)

set(CMAKE_C_STANDARD 11)

add_executable(untitled3 main.c
        parser.c
        token_shared.h
        parser.h)
