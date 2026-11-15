# Task 04 — Thread-safe bounded queue

**Language:** C++  
**Difficulty:** Hard

## Goal
Implement a blocking bounded FIFO queue. `push` blocks when full; `pop` blocks when empty. `close` wakes waiters; pushes fail after close; pop drains remaining items then returns false.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Implement class declared in `interfaces/api.hpp` using `solution.cpp`; operations must be race-free.

## Validation
`bash tests/run.sh`
