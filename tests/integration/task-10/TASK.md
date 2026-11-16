# Task 10 — JSON Pointer resolver

**Language:** Swift  
**Difficulty:** Hard

## Goal
Resolve RFC 6901-style JSON pointers against JSON-like Swift values. Decode ~0 to ~ and ~1 to /; reject invalid escapes and invalid array indices. Empty pointer returns root.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `Solution.swift`: `func resolvePointer(_ root: Any, _ pointer: String) -> Any?`. Support `[String: Any]`, `[Any]`, scalars.

## Validation
`bash tests/run.sh`
