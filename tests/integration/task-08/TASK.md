# Task 08 — Run-length encoding

**Language:** Swift  
**Difficulty:** Easy

## Goal
Encode consecutive equal characters as `(character, count)` tuples. Treat Swift Characters (extended grapheme clusters) as units.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `Solution.swift` with `func encodeRuns(_ text: String) -> [(Character, Int)]`.

## Validation
`bash tests/run.sh`
