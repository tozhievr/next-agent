# Task 25 — Accessible data table

**Language:** HTML  
**Difficulty:** Medium

## Goal
Create a semantic table of three products with a caption, column headers and row headers, using scope attributes and a thead/tbody.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.html` with `<table id="inventory">`; columns: Product, Price, Stock; rows: Pen/2/10, Book/5/3, Bag/20/1.

## Validation
`python3 tests/check.py`
