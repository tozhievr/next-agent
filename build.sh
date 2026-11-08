#!/usr/bin/env bash
# Build NextAgent (Windows x86-64). Usage: ./build.sh [test]
#   NA_COMPILER=nextc (default) | clang
#   NEXTC=path/to/nextc.exe   CLANG=path/to/clang.exe
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
LIB="src/util.c src/json.c src/platform.c src/core.c src/store.c src/c23.c src/lsp_proto.c
     src/lsp_build.c src/lsp_client.c src/lsp_gate.c src/lsp_hub.c src/refactor.c src/index.c src/semantic.c src/rpc.c"
cc() { # cc <main-file> <output>
    case "${NA_COMPILER:-nextc}" in
        clang) "${CLANG:-clang}" -std=c2x -O2 -Wall -Isrc $LIB "$1" -o "$2" -lkernel32 ;;
        nextc) "${NEXTC:-nextc}" -Isrc $LIB "$1" -o "$2" ;;
        *) echo "unknown NA_COMPILER" >&2; exit 2 ;;
    esac
}
cc src/main.c build/nextagent.exe
echo "built build/nextagent.exe (${NA_COMPILER:-nextc})"
if [ "${1:-}" = "test" ]; then
    cc tests/tests.c build/tests.exe
    ./build/tests.exe
fi
