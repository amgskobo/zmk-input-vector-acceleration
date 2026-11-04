#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
workspace="$1"
build_dir="$2"
log="$build_dir.log"
if (cd "$workspace" && west build -s zmk/app -d "$build_dir" \
    -b native_sim//zmk_test_mock -- \
    -DZMK_CONFIG=/src/tests/integration/custom-speed-guard \
    -DZMK_EXTRA_MODULES=/src) >"$log" 2>&1; then
    echo "FAILED: unrepresentable Custom Settings speed unexpectedly built"
    exit 1
fi
if ! grep -Fq 'max-speed exceeds the Custom Settings int32 range' "$log"; then
    echo "FAILED: unrepresentable speed did not fail its named range guard"
    tail -n 60 "$log"
    exit 1
fi
echo "PASS: unrepresentable Custom Settings speed rejected"
