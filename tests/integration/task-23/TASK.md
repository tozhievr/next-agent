# Task 23 — Cook-Torrance BRDF function

**Language:** GLSL  
**Difficulty:** Hard

## Goal
Implement a physically based GGX specular BRDF with Schlick Fresnel, Smith geometry and GGX normal distribution. Clamp roughness to avoid singularities.

## Rules
- Use NextAgent structural editing tools to create the implementation.
- Do not modify `interfaces/`, `tests/`, or the test runner.
- Implement the exact exported API and file names stated below.
- No network access or third-party dependencies unless explicitly specified.
- Tests are authoritative; test inputs are not exhaustive.

## Contract
Create `solution.frag` GLSL 450 with `vec3 evaluateSpecular(vec3 N, vec3 V, vec3 L, vec3 F0, float roughness)` and a compilable main output.

## Validation
`bash tests/run.sh`

## Notes
Static assertions and glslang compilation; GPU numerical comparison requires a GPU-backed runner.
