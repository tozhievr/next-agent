# Task 12 — Expression AST evaluator

**Language:** Haskell  
**Difficulty:** Medium

## Goal
Evaluate an arithmetic expression tree. Division by zero returns `Nothing`; integer division uses `div`.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Implement `eval :: Expr -> Maybe Integer` in `Solution.hs` using `Expr` from `interfaces/API.hs`.

## Validation
`bash tests/run.sh`
