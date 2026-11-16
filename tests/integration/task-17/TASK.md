# Task 17 — Discriminated event reducer

**Language:** TypeScript  
**Difficulty:** Medium

## Goal
Implement an exhaustive reducer for a typed shopping cart: add, remove, setQuantity. Quantities <=0 remove items; no mutation of original state.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.ts` exporting `reduceCart(state:Cart,event:CartEvent):Cart`.

## Validation
`bash tests/run.sh`
