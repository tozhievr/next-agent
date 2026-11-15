# NextAgent Integration Benchmark — 30 tasks

30 agent-facing coding tasks across nine languages, increasing from Easy to Hard.

## Layout
Each `task-NN/` contains `TASK.md`, `interfaces/`, `tests/`. The agent should see only `TASK.md` and `interfaces/`; the evaluator owns `tests/` and runs them in a separate trusted workspace. The shipped tests are **reference tests for benchmark authors** and must be withheld from the agent during evaluation.

## Execution
Implement the requested file in each task directory, then run the command under **Validation** in its TASK.md. No solutions are shipped intentionally. Tools: g++ C++20, kotlinc/JVM, swiftc, runghc, Node.js and `tsx` for TypeScript, glslangValidator for GLSL, Python 3 for HTML/CSS static checks.

## Benchmark protocol
1. Clone a clean workspace for each run; expose task instructions and interfaces, not tests.
2. Run agent with structural tools; record tool invocations, token usage, wall time, API cost and any unauthorized edits.
3. Apply resulting solution to the evaluator copy, compile and run tests in a sandbox with resource limits.
4. Record correctness separately from harness compliance; compare identical tasks across versions and seeds.
5. Add private adversarial tests before publishing comparative success rates. HTML/CSS checks are static, and GLSL checks compile but do not verify pixel output. Avoid claiming full semantic validation for those tasks.

## Known limitations
- The supplied suite is an initial runnable **test specification**, not a production-grade benchmark. Some checks are intentionally lightweight.
- No reference implementations are included, so compilation cannot be fully end-to-end verified until an agent supplies solutions.
- TypeScript requires the `tsx` package to be installed; runtime assertions alone do not enforce compile-time types. Add `tsc --noEmit` in CI for type checking.
- C++ `assert` tests should not be compiled with `-DNDEBUG`.
- For shader visual correctness, add GPU-backed rendering/image comparisons.
