#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Compile the base driver against upstream ZMK and the optional Studio adapter
# against the DYA ZMK fork with zmk-feature-custom-settings present.

set -euo pipefail

tests_dir=/src/tests/integration
source "$tests_dir/workspace-env.sh"

if [ -n "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ]; then
    work_dir="${ZMK_TEST_OUTPUT:?set ZMK_TEST_OUTPUT to a fresh evidence directory}"
    if [ -e "$work_dir" ]; then echo "output already exists: $work_dir" >&2; exit 2; fi
    # Each prepared view has its own fresh child, while build outputs remain
    # under the common evidence directory consumed by existing assertions.
    dya_dir="$(python3 /src/tests/integration/offline.py "$ZMK_TEST_OFFLINE_WORKSPACE" dya "$tests_dir/config/west.yml" "$work_dir/dya-inputs")"
    upstream_dir="$(python3 /src/tests/integration/offline.py "${ZMK_TEST_OFFLINE_UPSTREAM_WORKSPACE:?set the upstream workspace}" upstream "$tests_dir/upstream/west.yml" "$work_dir/upstream-inputs")"
else
if [ -n "${ZMK_TEST_WORKSPACE:-}" ]; then
    work_dir="$ZMK_TEST_WORKSPACE"
    mkdir -p "$work_dir"
else
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-vector-accel-integration.XXXXXX")"
    cleanup() {
        rm -rf "$work_dir"
    }
    trap cleanup EXIT HUP INT TERM
fi

fi

prepare_workspace() {
    local name="$1"
    local manifest="$2"
    local workspace="$work_dir/$name"

    mkdir -p "$workspace/config"
    cp "$manifest" "$workspace/config/west.yml"
    cd "$workspace"

    if [ ! -d .west ]; then
        west init -l config
    fi
}

build_case() {
    local workspace="$1"
    local case_name="$2"
    local config_dir="$3"
    local build_dir="$work_dir/build/$case_name"
    zmk_test_select_workspace "$workspace"

    rm -rf "$build_dir"
    mkdir -p "$build_dir"

    if ! (cd "$workspace" && ZEPHYR_BASE="$workspace/zephyr" west build -s zmk/app -d "$build_dir" \
        -b native_sim//zmk_test_mock -- -DZephyr_DIR="$workspace/zephyr/share/zephyr-package/cmake" -DCONFIG_ASSERT=y \
        -DZMK_CONFIG="$config_dir" -DZMK_EXTRA_MODULES="/src;$tests_dir/module") \
        >"$build_dir.log" 2>&1; then
        echo "FAILED: $case_name"
        tail -n 60 "$build_dir.log"
        return 1
    fi

    echo "PASS: $case_name"
}

if [ -z "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ]; then
prepare_workspace dya "$tests_dir/config/west.yml"
dya_dir="$work_dir/dya"
west update --narrow --fetch-opt=--depth=1
west zephyr-export

prepare_workspace upstream "$tests_dir/upstream/west.yml"
upstream_dir="$work_dir/upstream"
west update --narrow --fetch-opt=--depth=1 --path-cache "$dya_dir"
west zephyr-export

fi

build_case "$upstream_dir" upstream-smoke "$tests_dir/smoke"
build_case "$dya_dir" dya-studio "$tests_dir/studio"
build_case "$upstream_dir" upstream-runtime "$tests_dir/runtime"
build_case "$upstream_dir" upstream-persistence "$tests_dir/persistence"
zmk_test_select_workspace "$upstream_dir"
(cd "$upstream_dir" && west zephyr-export)
bash "$tests_dir/guards/run.sh" "$upstream_dir" "$work_dir/build/guards"
zmk_test_select_workspace "$dya_dir"
(cd "$dya_dir" && west zephyr-export)
bash "$tests_dir/firmware/run.sh" "$dya_dir" "$work_dir/build/firmware"

runtime_exe="$work_dir/build/upstream-runtime/zephyr/zmk.exe"
runtime_log="$work_dir/build/upstream-runtime.run.log"
if ! timeout 30 "$runtime_exe" >"$runtime_log" 2>&1; then
    echo "FAILED: upstream-runtime did not exit successfully"
    tail -n 60 "$runtime_log"
    exit 1
fi
if ! grep -Fq "vector acceleration runtime tests: PASS" "$runtime_log"; then
    echo "FAILED: upstream-runtime did not report success"
    tail -n 60 "$runtime_log"
    exit 1
fi
echo "PASS: upstream-runtime execution"

studio_exe="$work_dir/build/dya-studio/zephyr/zmk.exe"
studio_log="$work_dir/build/dya-studio.run.log"
if ! timeout 30 "$studio_exe" >"$studio_log" 2>&1; then
    echo "FAILED: dya-studio did not exit successfully"
    tail -n 60 "$studio_log"
    exit 1
fi
grep -Fq "vector acceleration Studio settings tests: PASS" "$studio_log"
echo "PASS: dya-studio execution"

persistence_exe="$work_dir/build/upstream-persistence/zephyr/zmk.exe"
persistence_log="$work_dir/build/upstream-persistence.run.log"
persistence_flash="$work_dir/build/upstream-persistence/flash.bin"
: >"$persistence_log"
for boot in 1 2; do
    if ! timeout 30 "$persistence_exe" --flash="$persistence_flash" \
        >>"$persistence_log" 2>&1; then
        echo "FAILED: upstream-persistence boot $boot"
        tail -n 60 "$persistence_log"
        exit 1
    fi
done
grep -Fq "vector acceleration persistence first boot: PASS" "$persistence_log"
grep -Fq "vector acceleration persistence second boot: PASS" "$persistence_log"
echo "PASS: upstream-persistence execution"

echo "vector acceleration integration: PASS"
