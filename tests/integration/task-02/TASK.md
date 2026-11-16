# Task 02 — UTF-8 code point counter

**Language:** C++  
**Difficulty:** Easy

## Goal
Validate UTF-8 and count Unicode scalar values, rejecting overlong encodings, surrogate code points, truncated sequences and values above U+10FFFF.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Implement `utf8_count(std::string_view)` in `solution.cpp`, returning `std::nullopt` for invalid input.

## Validation
`bash tests/run.sh`
