#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
swiftc interfaces/API.swift Solution.swift tests/Test.swift -o /tmp/nextagent_test_$$
trap 'rm -f /tmp/nextagent_test_$$' EXIT
/tmp/nextagent_test_$$
