# Task 07 — Dependency topological sort

**Language:** Kotlin  
**Difficulty:** Hard

## Goal
Return a deterministic topological ordering for tasks, selecting the lexicographically smallest currently available task. Return null on cycles or unknown dependencies.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `Solution.kt`: `fun orderTasks(tasks: Set<String>, dependencies: List<Pair<String,String>>): List<String>?`. Each pair `(task, prerequisite)`.

## Validation
`bash tests/run.sh`
