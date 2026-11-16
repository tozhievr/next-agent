#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
kotlinc interfaces/API.kt Solution.kt tests/Test.kt -include-runtime -d /tmp/nextagent_test_$$.jar
trap 'rm -f /tmp/nextagent_test_$$.jar' EXIT
java -jar /tmp/nextagent_test_$$.jar
