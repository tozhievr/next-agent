#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
g++ -std=c++20 -O2 -Wall -Wextra -pthread -Iinterfaces solution.cpp tests/test.cpp -o /tmp/nextagent_test_$$
trap 'rm -f /tmp/nextagent_test_$$' EXIT
/tmp/nextagent_test_$$
