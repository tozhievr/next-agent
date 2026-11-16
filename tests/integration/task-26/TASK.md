# Task 26 — Accessible modal dialog markup

**Language:** HTML  
**Difficulty:** Hard

## Goal
Build a no-JavaScript modal dialog skeleton with a descriptive title, explanatory text, a close form and correct ARIA relationships.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.html` with `<dialog id="confirm" aria-labelledby="dialog-title" aria-describedby="dialog-description">`, heading and paragraph with matching IDs, and a `<form method="dialog">` containing a close button.

## Validation
`python3 tests/check.py`

## Notes
Markup checks cannot prove actual assistive-technology behavior.
