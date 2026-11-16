# Task 21 — Grayscale fragment shader

**Language:** GLSL  
**Difficulty:** Easy

## Goal
Implement a GLSL 450 fragment shader that converts RGB to luminance using Rec.709 weights and preserves alpha.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.frag`; input `layout(location=0) in vec4 vColor;`, output `layout(location=0) out vec4 outColor;`.

## Validation
`bash tests/run.sh`

## Notes
Static assertions and glslang compilation; GPU output validation is not part of this portable suite.
