# Task 11 — Safe list rotation

**Language:** Haskell  
**Difficulty:** Easy

## Goal
Rotate a list to the left by n positions; negative n rotates right. Empty lists remain empty.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Implement `rotate :: Int -> [a] -> [a]` in `Solution.hs`, exporting module `Solution`.

## Validation
`bash tests/run.sh`
