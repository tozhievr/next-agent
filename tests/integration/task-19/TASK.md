# Task 19 — Event emitter

**Language:** JavaScript  
**Difficulty:** Medium

## Goal
Implement `on`, `off`, `once`, `emit`; listeners run in registration order. Mutations during emit must not change the current emission snapshot.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.mjs` exporting class `Emitter`.

## Validation
`node tests/test.mjs`
