# Nearoh Coding Language

A Python-inspired programming language written in C, focused on real usability, clean architecture, and long-term growth into a serious everyday language.

Nearoh is not a toy parser or throwaway syntax project. It is being built as a practical language/runtime I would genuinely want to use—while maintaining full control over internals, performance paths, and future low-level expansion.

Website:
https://nearoh-coding-language.base44.app

GitHub:
https://github.com/ReeceGilbert/Nearoh-Coding-Language

---

# Vision

Nearoh aims to combine the productivity and readability of Python with the control and extensibility of a C-backed runtime.

## Core Goals

- Familiar Python-style workflow
- Clean readable syntax
- Classes, objects, methods, and productivity-first scripting
- Runtime written in C for control and performance
- Expandable native bridge for machine-facing systems
- Long-term editor / IDE environment built around the language
- Strong architecture over rushed features

This project is being built to become a serious personal-use language, not just a demo.

---

# Current Status

## Major Milestone Reached

Nearoh now runs real multi-feature programs from the command line.

Implemented systems include:

- Lexer
- Parser
- AST generation
- Runtime evaluator
- Scope / environment model
- Variables and reassignment
- Arithmetic
- Strings
- Numbers
- Booleans
- if / else
- while loops
- Functions
- Return values
- Classes
- Automatic `__init__` constructors
- Object fields
- Member access
- Bound `self` methods
- Lists
- List indexing
- List index assignment
- `for ... in` list iteration
- Builtin `print()`
- Builtin `len()`

## CLI Modes

Nearoh now supports:

```bash
nearoh examples/hello.nr
nearoh --tokens examples/hello.nr
nearoh --ast examples/hello.nr
nearoh --debug examples/hello.nr


⸻

Example Programs

The repository includes runnable examples:
	•	examples/hello.nr
	•	examples/variables.nr
	•	examples/functions.nr
	•	examples/classes.nr
	•	examples/lists.nr
	•	examples/loops.nr
	•	examples/objects_and_lists.nr
	•	examples/arena_showcase.nr

⸻

Showcase Example

class Vector2():
    def __init__(self, x, y):
        self.x = x
        self.y = y

points = [Vector2(1, 2), Vector2(3, 4)]

for p in points:
    print(p.x)
    print(p.y)

items = [10, 20, 30]
items[1] = 99

print(items[1])
print(len(items))
print(len("Nearoh"))

Expected Output:

1
2
3
4
99
3
6


⸻

Arena Showcase

arena_showcase.nr is a larger demonstration program proving that Nearoh can coordinate multiple systems together.

It uses:
	•	Classes
	•	Lists of objects
	•	Constructors
	•	Object state mutation
	•	Functions
	•	Conditional logic
	•	While loops
	•	For loops
	•	Runtime score tracking
	•	Multi-round battle simulation logic

This moves Nearoh beyond syntax demos into real executable projects.

⸻

Why This Project Matters

Many hobby language projects stop at parsing expressions.

Nearoh already includes real runtime behavior:
	•	Executable programs
	•	User-defined functions
	•	Object-oriented systems
	•	Dynamic lists
	•	Scope handling
	•	Builtins
	•	Structured examples
	•	Command-line tooling

That means the project is moving into genuine language engineering territory.

⸻

Roadmap

Near-Term
	•	Dictionaries / maps
	•	Better runtime error messages
	•	Cleaner diagnostics with line numbers
	•	Standard library utilities
	•	File I/O

Mid-Term
	•	Modules / imports
	•	Expanded builtins
	•	Better performance paths
	•	Improved memory systems
	•	Tooling improvements

Long-Term
	•	Native graphics / window bridge
	•	Input / timing systems
	•	Bytecode VM or compiled backend research
	•	Dedicated Nearoh editor / IDE
	•	Potential self-hosted growth path

⸻

Philosophy

Nearoh is being built carefully and intentionally.

The goal is not to copy Python line-for-line.

The goal is to preserve what makes Python productive while gaining deeper ownership of the machine underneath it.

Readable high-level development on top.
Low-level power underneath.

⸻

Author

Built by Reece Gilbert.

This project reflects years of programming curiosity, systems experimentation, graphics work, simulation building, and the drive to create something real from scratch.

⸻

Current Stage

Nearoh is early, active, and growing quickly.

Every milestone is focused on turning it into a real usable language rather than a superficial prototype.