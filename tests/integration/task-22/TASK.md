# Task 22 — Normal transformation vertex shader

**Language:** GLSL  
**Difficulty:** Medium

## Goal
Transform a vertex position with model/view/projection matrices and transform normals with inverse-transpose of the model 3x3. Normalize output normal.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.vert`; inputs position location 0, normal location 1; uniforms binding 0 block containing mat4 model, view, projection; output normal location 0.

## Validation
`bash tests/run.sh`

## Notes
Static assertions and glslang compilation; GPU output validation is not part of this portable suite.
