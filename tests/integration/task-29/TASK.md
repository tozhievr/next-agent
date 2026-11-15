# Task 29 — Container-query component

**Language:** CSS  
**Difficulty:** Hard

## Goal
Build a self-contained product card using CSS container queries: default vertical layout; at container width >= 480px switch to two-column layout. Ensure media scales without overflow.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.css` with `.product-container` container-type:inline-size and `.product-card` layout, plus `@container (min-width: 480px)` override.

## Validation
`python3 tests/check.py`

## Notes
Static checks do not replace browser rendering tests.
