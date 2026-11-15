# Task 15 — Deep immutable merge

**Language:** TypeScript  
**Difficulty:** Medium

## Goal
Recursively merge plain objects; arrays and primitives replace prior values; never mutate either input; prevent prototype pollution for `__proto__`, `constructor`, `prototype`.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.ts`: export `deepMerge<T extends Record<string,unknown>>(base:T, patch:Record<string,unknown>):T`.

## Validation
`bash tests/run.sh`
