#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -f "$TEMP/test"; rmdir "$TEMP"' EXIT
for count in 2 14; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
        -DDD_MAX_PERIPHERALS="$count" -I"$ROOT/include" \
        "$ROOT/src/status_model.c" "$ROOT/tests/status_model_test.c" -o "$TEMP/test"
    "$TEMP/test"
done
