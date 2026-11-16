# Task 30 — Arithmetic expression lexer

**Language:** C++  
**Difficulty:** Medium

## Goal
Tokenize integers, + - * / and parentheses; skip ASCII whitespace; report invalid characters with byte offset. This extends the original user-supplied lexer test concept.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.cpp` implementing `lex(std::string_view)` from `interfaces/api.hpp`. Integers have type Number and their original lexeme. All offsets are zero-based bytes.

## Validation
`bash tests/run.sh`
