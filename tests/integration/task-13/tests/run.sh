#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cp interfaces/API.hs API.hs
trap 'rm -f API.hs API.hi API.o Solution.hi Solution.o tests/Main.hi tests/Main.o' EXIT
runghc -i. tests/Main.hs
