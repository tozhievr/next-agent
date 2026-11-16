# Task 13 — Dijkstra shortest paths

**Language:** Haskell  
**Difficulty:** Hard

## Goal
Compute shortest distances from a source in a directed nonnegative weighted graph. Ignore unreachable nodes; parallel edges are allowed.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Implement `shortestPaths :: Ord a => a -> [(a,a,Int)] -> [(a,Int)]` in `Solution.hs`, output sorted by vertex.

## Validation
`bash tests/run.sh`
