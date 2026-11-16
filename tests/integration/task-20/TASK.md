# Task 20 — Retry with exponential backoff

**Language:** JavaScript  
**Difficulty:** Hard

## Goal
Implement async retry with exponential delays, optional injected sleep function, and stop after maxAttempts. Do not retry after success.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.mjs` exporting `retry(fn,{maxAttempts,baseDelayMs,sleep})`. Delay before retry i is baseDelayMs * 2^(i-1). maxAttempts >=1.

## Validation
`node tests/test.mjs`
