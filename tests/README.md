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
| [test_vector_accel.c](test_vector_accel.c) | Gain interpolation, vector direction, saturation, rounding and bounded time-history storage |
| [runtime/driver_harness.c](runtime/driver_harness.c) | Frame timing, independent histories and persistence callbacks |
| [runtime/driver_ram_harness.c](runtime/driver_ram_harness.c) | RAM-only persistence variant |
| [runtime/custom_settings_harness.c](runtime/custom_settings_harness.c) | Custom-settings persistence owner |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Actual runtime entry rejects empty, unrelated and partially measured gcov reports |
| [test_vector_width.py](test_vector_width.py) | 32-bit object arithmetic helpers and named time-history capacity boundary rejection |

The core checks modulo-32-bit timestamp endpoints, long forward gaps, backwards-clock resets and history continuity across wrap. This clock domain intentionally replaces the former signed-64-bit domain; prior fixtures and passing logs are retained by the consuming integration owner.

The time-history fixture compares 12,000 ordered frames with independent wide
state. It reaches the 200 ms rounded fixed point, exercises all intervals
0–100 ms, forward gaps, backwards time and wrap, and checks the matching gain
after every frame. A named 24-byte stream layout assertion rejects the original
28-byte layout. The production compile-time bound also rejects constants that
cannot fit the time-history storage. Listener slots, fallback remainders,
timestamp width, curve ranges and persistence layouts retain their contracts.

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
Workspace selection and its infrastructure tests keep the selected dependency
providers explicit.

Custom Settings speeds use the registry's positive int32 range. The host
registration-macro test admits defaults above one million and at INT32_MAX,
then rejects an unrepresentable default with its named build error. The RAM
driver fixture checks matching runtime acceptance/rejection and unchanged state
on rejection. The DYA registry fixture writes those high speeds through the
actual registry; integration also retains a real DTS rejection build. The base
driver's uint32 arithmetic and supported-core variants remain covered.

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

## Optional source-prefix capacity

The canonical integration entry accepts no arguments and rejects ambient
`ZMK_TEST_SOURCE_BOUND_PROFILE`/`ZMK_VECTOR_SOURCE_PROFILE` selectors. It runs
the complete existing matrix and then all twelve `tests/source-bound/run.py`
profiles on both unchanged stock and DYA. High IDs 3/4 remain covered in auto;
low IDs 0/1 run in auto and with `MAX_SOURCES=2`; `256` exercises every byte ID.
Every runtime profile requires three identical-binary completions, two vector
nodes, independent source histories, NULL slot zero, unchanged invalid input,
curve changes and a compiled assertion of actual driver capacity. Portable
quick/full also test all 256 IDs under limits 1, 2 and 256 with ASan/UBSan;
full retains optimization and measured coverage.

Real DTS guards reject high-index default, child, disabled-child and second-node
routes and exact prefix endpoints at limits 1 and 3. Disabled listeners and
disabled referenced devices remain permitted. Four owner guard mutants qualify
only when the validator reports its named
missing routed-listener rejection, with successful mutant compilation.
`control-original.py` separately requires the unchanged before-limit driver to
hit the named compiled two-slot capacity assertion against a completed low-2
build; unrelated compiler errors do not qualify.

For focused diagnosis only, inside the same prepared Docker environment:

```sh
python3 tests/source-bound/run.py --workspace RESOLVED --core stock \
  --output FRESH --profile low-2
python3 tests/source-bound/control-original.py --build COMPLETED/low-2/build \
  --original-driver RETAINED_ORIGINAL/src/input_processor_vector_accel.c --output FRESH_CONTROL
```

The standalone selector is explicit; it never establishes the complete owner
matrix. Recompile original/default/limited ARM objects from the emitted board
commands and matching generated headers to separate actual static storage,
code size and compiler frames from installed runtime high-water. Hardware,
browser and hosted-CI gates remain separate. The option, byte costs, direct
caller obligations and remeasurement requirements are documented in both READMEs.

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

## 32-bit arithmetic and clock contract

Vector products, normalized ratios, speed and saturated signed output use 32-bit arithmetic. Rare full-range ratios use bounded binary fractional division. Core clock parameters and last_report_time_ms are uint32 milliseconds; migrate external core callers by passing the low 32 bits of monotonic uptime. Histories remain valid across wrap for intervals at most 100 ms. An entire 2^32 ms of silence cannot be distinguished from a short interval.

test_vector_accel.c retains independent wide arithmetic references and gain/remainder/saturation cases, adds cancellation at signed limits and checks long gaps, backwards times and wrap continuity. test_vector_width.py rejects wide helpers in a 32-bit object.
