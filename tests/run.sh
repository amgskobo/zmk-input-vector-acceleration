#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Run the dependency-free vector calculation and stream-state contracts.

set -euo pipefail

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-vector-accel-test.XXXXXX")"

cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

warnings=(-std=c11 -Wall -Wextra -Werror -pedantic -Wshadow
    -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wconversion -Wsign-conversion)

printf '#include <zmk-input-vector-acceleration/vector_accel_core.h>\n' \
    >"$build_dir/header.c"
cc "${warnings[@]}" -fsyntax-only -I"$repo_root/include" "$build_dir/header.c"

variants=(
    "optimised:-O2"
    "sanitized:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
    "32-bit:-O2 -m32"
)

export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

for variant in "${variants[@]}"; do
    label="${variant%%:*}"
    read -r -a flags <<<"${variant#*:}"

    cc "${warnings[@]}" "${flags[@]}" -I"$repo_root/include" \
        "$repo_root/src/vector_accel_core.c" "$repo_root/tests/test_vector_accel.c" \
        -o "$build_dir/test-$label"
    printf 'vector acceleration (%s): ' "$label"
    "$build_dir/test-$label"
done
