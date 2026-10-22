# Module-owned tests

```bash
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
```

Quick runs every scenario under ASan/UBSan; full adds optimized, coverage and
32-bit variants. Unknown modes fail. Resolve/read actual dependency instructions
before testing. [contract.json](contract.json) and [maintenance.json](maintenance.json)
map current fixtures, commands, supported cores and gaps.

## Retained behavioral contracts

| Fixture | Assertions |
| --- | --- |
| [test_vector_accel.c](test_vector_accel.c) | Gain interpolation, vector direction, saturation and rounding |
| [runtime/driver_harness.c](runtime/driver_harness.c) | Frame timing, independent histories and persistence callbacks |
| [runtime/driver_ram_harness.c](runtime/driver_ram_harness.c) | RAM-only persistence variant |
| [runtime/custom_settings_harness.c](runtime/custom_settings_harness.c) | Custom-settings persistence owner |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Actual runtime entry rejects empty, unrelated and partially measured gcov reports |

The core also checks monotonic and backwards transitions across the full signed
timestamp range. A span beyond the history window resets velocity without a
signed subtraction overflow, for both opening and closing a frame. This is a
pure timestamp-domain regression, not evidence of a board clock fault.

Each fixture keeps its existing normal, boundary/invalid, independent-state,
reset/cancellation and failure/recovery scenarios where applicable. Checks
assert ordered outputs and explicit time inputs. Coverage is a full-mode gate,
not a reason to replace assertions or a hardware completeness claim.

Both host modes and CI run the synthetic coverage-admission controls. Each
gated real function needs measured executable lines; macro expansions remain
measured in their callers. Normal full runs retain actual compiled line/branch
measurement and all behavioral assertions. Recording tools prove admission,
not firmware behavior.

## Responsibility map

This owner contains no source-button suppression, pair history, layer-policy
watcher or final-filter dependency. It consumes relative X/Y for gain and keeps
source-local frame timing/history and fractional remainders. Other event payloads
and sync remain unchanged; a sync event may close an already open motion frame.

Those histories determine speed/gain and cannot move into final suppression.
The fallback remainders are required for callers without listener remainder
storage. Curve snapshot locking and delayed save work own runtime tuning. The
custom-settings build excludes local persistence so only one writer is active.
Production state/API/DTS/Kconfig remain unchanged. The current review adds
explicit workspace selection and its infrastructure tests.

| Boundary | Retained fixture/assertions |
| --- | --- |
| Pure vector math/history | test_vector_accel.c links actual vector_accel_core.c: same gain/diagonal/saturation/fractional/timestamp extremes and frame reset assertions |
| Driver and fallback histories | runtime/driver_harness.c: same event frames, source isolation, runtime curve and local persistence callbacks |
| Alternate persistence owner | runtime/driver_ram_harness.c: same RAM-only variant; custom-settings owns writes |
| Settings adapter | runtime/custom_settings_harness.c: same types/ranges/key/application/reset checks |
| Whole real compatibility / binding guards | Existing integration/run.sh supported stock and DYA variants, selected for API/build changes |

## Integration and admission

Both unchanged stock ZMK and DYA are supported. Keep the owner integration/API/
DTS guards for interface, registration, settings or kernel changes. Inside the
pinned image, mount this module at `/src` and explicitly select resolved caches.
The offline entry creates isolated unchanged ZMK clones, dependency symlinks and
original fixture manifests without fetching. It checks exact clean core, Zephyr
and required library identities before building.

DYA enables custom settings. Supply its unmodified pinned git archive via
`ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE`; [integration/offline.py](integration/offline.py)
records its revision/digest. Host/CI admission tests retain wrong/dirty/missing
pin and parseable wrong-archive rejection, safe project paths, output outside
sources/dependencies (including parent symlinks), and separate west/config
metadata. External dependencies and sibling outputs remain supported. Identity/
digest bypass controls must fail their named missing-rejection assertions.

A complete offline run in the build image uses both selected cores:

```bash
ZMK_TEST_OFFLINE_WORKSPACE=/resolved/dya \
ZMK_TEST_OFFLINE_UPSTREAM_WORKSPACE=/resolved/upstream \
ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE=/resolved/custom-settings-c6a7fef.tar \
ZMK_TEST_OUTPUT=/tmp/vector-acceleration-integration-v1 \
bash /src/tests/integration/run.sh
```

## Concurrent registry updates

The settings adapter serializes each complete registry read/apply transaction,
including initialization. The DYA Studio fixture pauses writer A after the last
real registry read, commits another field from higher-priority writer B, then
resumes A. Both writes must finish with the live curve matching the latest
registry values. The same before-repair binary fails this assertion three times;
the integration entry now repeats the unchanged Studio binary three times and
requires both the old completion marker and the concurrent-update marker.
The linker wrapper belongs only to the test module. All previous rejection,
curve, role, firmware, persistence and host assertions remain. Host stand-ins
check that the transaction lock covers read and apply and is released afterward.
This is real registry/kernel evidence, not a browser RPC or physical flash test.

## Retired simulation and physical gaps

The split-steady simulation source and raw ordered snapshots were externally
archived before removal. State/arithmetic/error assertions remain above; exact
radio/HID/LED/battery traces, physical clock/arrival jitter, NVS power-loss
persistence, USB/BLE/split delivery, timing, power and recovery need separate
matching board evidence. No failed snapshot was accepted. The earlier documentation
consolidation retained all assertions. Current workspace guards change no
behavioral snapshot, API or production state.

## Selected workspace and module providers

Workspace selection uses tests/integration/workspace-env.sh to export the
selected manifest's ZEPHYR_MODULES and explicit Zephyr package/base paths. A
Zephyr symlink to a different cache must not select that cache's module providers.
The owner test_workspace_env.py exercises actual selection with a poisoned ambient
list, a cache symlink, module paths containing spaces, failed project discovery and
an empty selection. The keep-ambient negative control must fail these assertions.
Full integration retains all supported-core, role, persistence and invalid-DTS
cases. No dependency is patched or required feature disabled.
