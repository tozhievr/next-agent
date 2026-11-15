# Task 03 — LRU cache

**Language:** C++  
**Difficulty:** Medium

## Goal
Build a capacity-bounded least-recently-used cache. `get` refreshes recency. `put` updates existing entries and evicts least recently used when full.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Implement `interfaces/api.hpp` class `LRUCache` in `solution.cpp`; constructor accepts positive capacity. `get` returns optional int.

## Validation
`bash tests/run.sh`
