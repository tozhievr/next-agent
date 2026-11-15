# Task 06 — Stable event aggregation

**Language:** Kotlin  
**Difficulty:** Medium

## Goal
Group events by user; sum signed amounts; preserve first-seen user order. Empty input returns empty list.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `Solution.kt` implementing `aggregate(events: List<Event>): List<UserTotal>` using data classes from `interfaces/API.kt`.

## Validation
`bash tests/run.sh`
