#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/check.py
glslangValidator -V solution.* -o /tmp/nextagent_shader_$$.spv >/dev/null
trap 'rm -f /tmp/nextagent_shader_$$.spv' EXIT
