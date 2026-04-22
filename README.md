# Nearoh Coding Language

A Python-inspired language project written in C, built with the long-term goal of becoming a usable everyday language for my own workflow.

This project is not meant to be a toy parser or a throwaway syntax experiment. The goal is to build a real language/runtime with Python-like semantics, a clean internal architecture, and a minimal native C bridge for machine-facing capabilities that higher-level libraries can be built on top of later.

Website:
https://nearoh-core-lang.base44.app

---

## Vision

The long-term goal is to create a language that:

- Feels familiar and productive like Python
- Preserves Python-style classes and core semantics
- Allows alternate surface syntax only when it maps 1:1 to the same meaning
- Is implemented in C so the runtime and low-level behavior stay fully under control
- Exposes a minimal native layer for core capabilities like memory/runtime primitives, timing, files, input, graphics/windowing, and other foundational machine-facing systems
- Can eventually power its own editor / IDE-like environment for everyday use

This is meant to grow into something I would genuinely want to use, not just something that parses code for demonstration.

---

## Current Status

Frontend foundation complete. Runtime core now functional and executing real programs.

### Implemented frontend systems

- Lexer / tokenizer
- Indentation-aware block tokens (`INDENT` / `DEDENT`)
- Expressions with operator precedence
- Variables / assignment parsing
- Function definitions
- Class definitions
- `if / elif / else`
- `while`
- `for ... in`
- Function calls
- Member access (`object.value`, `object.method()`)
- AST generation
- AST debug printing

### Implemented runtime systems

#### Core execution

- Runtime value representation
- Variable environments
- Expression evaluation for literals / identifiers / grouping
- Unary operators (`-`, `not`)
- Binary arithmetic
- Comparisons
- Logical operators
- Assignment execution
- `if / else` execution
- `while` execution
- `return` / `break` / `continue` control-flow signaling

#### Functions

- Builtin function calls (`print`)
- User-defined function execution
- Parameters
- Return values
- Function-local scope / call frames

#### Classes / Objects

- Class definitions
- Class instantiation
- Instance field assignment
- Instance field access
- Methods
- Automatic bound `self`

At the current stage, the language can successfully execute programs involving variables, arithmetic, reassignment, branching, loops, functions, classes, objects, member access, and methods.

---

## Project Structure

    main.c        - Entry point / pipeline driver

    lexer.c       - Tokenization
    lexer.h

    parser.c      - Syntax parsing
    parser.h

    ast.c         - AST utilities / printing / cleanup
    ast.h

    runtime.c     - Runtime execution engine
    runtime.h

    value.c       - Runtime values
    value.h

    env.c         - Variable environments / scope storage
    env.h

    builtins.c    - Builtin registration
    builtins.h

    token.h       - Shared token definitions

    CMakeLists.txt

---

## Current Phase

Frontend architecture complete. Working runtime foundation established.

### Current runtime milestone

- Execute real programs correctly
- Prove expression evaluation
- Prove variable mutation
- Prove branching and loop execution
- Builtin function calls
- User-defined functions
- Function-local scope / call frames
- Class instances / objects
- Member access execution
- Member assignment
- Methods with bound `self`

### Next likely milestones

- `__init__` constructor support
- Better runtime error messages
- Lists / dictionaries
- `for ... in` execution
- Imports / modules
- Better memory cleanup / ownership
- Native C bridge
- Editor / IDE tooling

---

## Design Direction

This project is being built in layers:

1. Frontend foundations  
   Lexer, parser, AST, syntax structure

2. Runtime core  
   Values, environments, execution, control flow

3. Language usability  
   Functions, objects, builtins, modules

4. Native substrate  
   Low-level C bridge for machine-facing systems

5. Everyday workflow tooling  
   Editor / IDE-like environment, libraries, and practical usability

The priority is a strong core and clean architecture first, rather than rushing surface-level features.

---

## Why This Exists

A lot of languages share similar core ideas with different syntax wrapped around them. This project comes from wanting something that keeps the productivity and semantics I like, while also giving me more direct control over the runtime and the system underneath it.

The end goal is not just “a language that looks different.”  
The end goal is a language I would actually choose to use.

---

## Build

Example CMake build flow:

```bash
cmake -S . -B build
cmake --build build
