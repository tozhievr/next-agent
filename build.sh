#!/usr/bin/env bash
# Build NextAgent with nextc (Windows x86-64 target). Usage: ./build.sh [test]
# Set NEXTC to the compiler path if it is not on PATH.
set -euo pipefail
cd "$(dirname "$0")"
NEXTC="${NEXTC:-nextc}"
mkdir -p build
LIB="src/util.c src/json.c src/platform.c src/core.c src/store.c src/c23.c src/lsp_proto.c
     src/lsp_build.c src/lsp_client.c src/lsp_gate.c src/rpc.c"
# shellcheck disable=SC2086
"$NEXTC" -Isrc $LIB src/main.c -o build/nextagent.exe
echo "built build/nextagent.exe"
if [ "${1:-}" = "test" ]; then
    # shellcheck disable=SC2086
    "$NEXTC" -Isrc $LIB tests/tests.c -o build/tests.exe
    ./build/tests.exe
fi
