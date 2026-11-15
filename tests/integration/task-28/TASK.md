# Task 28 — Accessible focus and reduced motion

**Language:** CSS  
**Difficulty:** Medium

## Goal
Style `.action` with a visible `:focus-visible` outline and a hover transition, and disable transitions/animations under `prefers-reduced-motion: reduce`.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.css`. Use `@media (prefers-reduced-motion: reduce)` and `outline` for keyboard focus.

## Validation
`python3 tests/check.py`
