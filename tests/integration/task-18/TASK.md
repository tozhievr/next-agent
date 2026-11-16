# Task 18 — Query-string parser

**Language:** JavaScript  
**Difficulty:** Easy

## Goal
Parse a URL query string into an object of arrays, preserving duplicate keys and empty values. Decode percent escapes and plus as spaces. Reject malformed percent encoding.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.mjs` exporting `parseQuery(input)`; input may start with `?`.

## Validation
`node tests/test.mjs`
