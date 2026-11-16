# Task 16 — Promise pool

**Language:** TypeScript  
**Difficulty:** Hard

## Goal
Execute asynchronous jobs with at most `limit` jobs active. Preserve input order in results; reject if any job rejects. `limit` must be positive.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.ts` exporting `promisePool<T>(jobs:Array<()=>Promise<T>>,limit:number):Promise<T[]>`.

## Validation
`bash tests/run.sh`
