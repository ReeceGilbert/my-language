# my-language

A Python-inspired programming language being built from scratch in C.

## Vision

The long-term goal of **my-language** is to create a usable everyday language with Python-style semantics, readable syntax, and direct low-level extensibility through a native C runtime layer.

This project is not meant to be a toy parser. It is being built as a real foundation that can grow into:

* A full interpreter/runtime
* Custom standard libraries
* Native machine-facing modules
* Graphics / input / file system capabilities
* A dedicated editor / IDE experience
* Alternate surface syntax with Python-equivalent meaning

## Current Status

Early frontend foundation complete.

Current implemented systems:

* Lexer / tokenizer
* Indentation-aware block tokens (`INDENT` / `DEDENT`)
* Expressions with operator precedence
* Variables / assignment parsing
* Function definitions
* Class definitions
* `if / elif / else`
* `while`
* `for ... in`
* Function calls
* Member access (`object.value`, `object.method()`)
* AST generation
* AST debug printing

## Project Structure

```text
main.c      - Entry point / pipeline driver
lexer.c     - Tokenization
lexer.h

parser.c    - Syntax parsing
parser.h

ast.c       - AST utilities / printing
ast.h

token.h     - Shared token definitions

CMakeLists.txt
```

## Example Syntax

```python
x = 5
y = x + 10

def add(a, b):
    return a + b

class Counter:
    def tick(self):
        self.value = self.value + 1

if x > 0:
    print(x)
else:
    print(0)
```

## Build

Using CMake:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Current Phase

Frontend architecture cleanup complete.

Next likely milestones:

* Runtime / execution engine
* Variables and environments
* Function execution
* Class instances / objects
* Imports / modules
* Native C bridge
* Editor / IDE tooling

## Philosophy

Build clean systems first.
Own the stack.
Keep it expandable.
Make it real.

## Author

Reece Gilbert
