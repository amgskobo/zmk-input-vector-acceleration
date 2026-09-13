#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

workspace="$1"
build_root="$2"
firmware_module=/src/tests/integration/firmware

build() {
    local shield="$1"
    local build_dir="$build_root/$shield"
    local log="$build_root/$shield.log"

    rm -rf "$build_dir"
    if ! (cd "$workspace" && west build -s zmk/app -d "$build_dir" \
        -b xiao_ble/nrf52840/zmk -- -DZMK_EXTRA_MODULES="/src;$firmware_module" \
        -DSHIELD="$shield") >"$log" 2>&1; then
        echo "FAILED: $shield"
        tail -n 60 "$log"
        return 1
    fi

    test -f "$build_dir/zephyr/zmk.uf2"
    strings "$build_dir/zephyr/zmk.elf" >"$build_dir/strings.txt"
}

check_settings() {
    local shield="$1"
    local strings_file="$build_root/$shield/strings.txt"

    grep -Fxq amgskobo__accel "$strings_file"
    grep -Fxq fw_vector_accel.min_factor "$strings_file"
    grep -Fxq fw_vector_accel.max_factor "$strings_file"
    grep -Fxq fw_vector_accel.unity_speed "$strings_file"
    grep -Fxq fw_vector_accel.max_speed "$strings_file"
}

mkdir -p "$build_root"

for shield in vector_accel_test vector_accel_split_left vector_accel_split_right; do
    build "$shield"

    if [ "$shield" = vector_accel_split_right ]; then
        if grep -Fq amgskobo__accel "$build_root/$shield/strings.txt"; then
            echo "FAILED: split peripheral publishes Studio settings"
            exit 1
        fi
    else
        check_settings "$shield"
    fi

    echo "PASS: $shield"
done
