# Task 27 — Responsive card grid

**Language:** CSS  
**Difficulty:** Easy

## Goal
Create a responsive grid that automatically fits cards of minimum width 240px and fills available space.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.css` with `.cards` display:grid and `grid-template-columns: repeat(auto-fit, minmax(240px, 1fr))`.

## Validation
`python3 tests/check.py`
