# Task 09 — Interval union

**Language:** Swift  
**Difficulty:** Medium

## Goal
Merge overlapping or touching closed integer intervals, return sorted canonical intervals. Reject reversed intervals by ignoring them.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `Solution.swift` implementing `mergeIntervals(_ values: [Interval]) -> [Interval]` using `Interval` from `interfaces/API.swift`.

## Validation
`bash tests/run.sh`
