# ZMK Input Vector Acceleration

[![Test](https://github.com/amgskobo/zmk-input-vector-acceleration/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-vector-acceleration/actions/workflows/test.yml)

`zmk-input-vector-acceleration` is a ZMK input processor for relative pointer
motion. It calculates one acceleration factor from the combined X/Y vector and
applies that same factor to both axes. Diagonal motion therefore keeps its
direction instead of being distorted by independent per-axis acceleration.

The implementation uses integer and fixed-point arithmetic only. State and
fractional remainders are independent for each input listener/device stream.
An invalid runtime listener index is passed through and never aliases stream
zero.
It does not inspect layers and requires no notification from a keyboard,
trackpad driver, or keymap behavior.

## How it works

For each synchronized input report the processor:

1. Collects all relative X and Y deltas in the report.
2. Approximates the vector magnitude as `max(|x|, |y|) + 3/8 * min(|x|, |y|)`.
3. Converts it to counts per second from the report interval.
4. Calculates a quadratic fixed-point gain.
5. Uses that common gain for both axes of the next report.

The one-report delay is intentional. ZMK input processors receive X and Y as
separate events, and the first axis cannot know the second axis value without
buffering and re-emitting the whole report. Reusing the previous report's gain
keeps the processor small, preserves event order, and avoids a work queue or
proxy input device. Establishing an interval takes two reports, so the first
two reports at startup and after more than 100 ms of inactivity use 1.0x gain.

ZMK selects a layer override separately for each event. If a layer changes
between X and the synchronized Y event, the processor may miss the sync that
would close its frame. A repeated axis identifies that incomplete frame; it is
discarded instead of being combined with the next report.

## Configuration

Add the module to `config/west.yml`:

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-vector-acceleration
      remote: amgskobo
      revision: main
```

Include the processor node from the shield overlay:

```dts
#include <zmk-input-vector-acceleration/input_processor_vector_accel.dtsi>
```

Configure its four parameters. Factors are thousandths; speeds are vector
counts per second.

```dts
&vector_accel {
    min-factor = <500>;   /* 0.5x at zero speed */
    max-factor = <3200>;  /* 3.2x at max-speed and above */
    unity-speed = <1200>; /* exactly 1.0x */
    max-speed = <6000>;   /* reaches max-factor */
};
```

Then place `&vector_accel` after the processor that produces relative X/Y and
before inertia or other processors that should receive accelerated motion:

```dts
input-processors = <&zip_absolute_to_relative>,
                   <&vector_accel>,
                   <&zip_inertia>;
```

Only `INPUT_EV_REL` events with `INPUT_REL_X` or `INPUT_REL_Y` are modified.
Buttons, absolute coordinates, and horizontal/vertical wheel events pass
through unchanged. The supplied node enables ZMK remainder tracking, so
sub-count motion from factors below 1.0 is retained.

### Parameter reference

| Property | Allowed values | Meaning |
| --- | ---: | --- |
| `min-factor` | 100–1000 | Gain at zero speed, in thousandths |
| `max-factor` | 1000–20000 | Gain at `max-speed` and above, in thousandths |
| `unity-speed` | greater than 0 | Vector speed where gain is exactly 1.0 |
| `max-speed` | greater than `unity-speed` | Vector speed where `max-factor` is reached |

The curve is quadratic from `min-factor` to 1.0x, then quadratic from 1.0x to
`max-factor`. Invalid relationships stop the firmware build with a clear
message.

## Runtime configuration

The devicetree values are defaults. `vector_accel_runtime.h` lets firmware
change the curve while the keyboard is in use:

```c
#include <zmk-input-vector-acceleration/vector_accel_runtime.h>

struct vector_accel_config config;

vector_accel_get_config(dev, &config);
config.max_factor = 4000;
vector_accel_set_config(dev, &config);   /* -EINVAL leaves the processor untouched */
```

`vector_accel_set_config()` checks the same bounds the devicetree
`BUILD_ASSERT`s enforce, through `vector_accel_config_valid()` in the pure
core. A value the build would have rejected is rejected here too, and the
processor keeps running on its previous configuration.

When `CONFIG_SETTINGS` is enabled without the optional custom-settings
integration, calls to `vector_accel_set_config()` persist under the
`vaccel/<instance>` key. A stored value is applied after every processor has
taken its devicetree defaults. A value that no longer matches the struct or
falls outside the bounds is ignored, so the processor keeps a usable
devicetree curve.

When custom-settings integration is enabled, that registry is the only
persistence owner instead. Changes made through the registry persist under its
node-based keys and are then applied through the same runtime API. A direct
call to `vector_accel_set_config()` still changes the running curve, but does
not create a second stored copy that could disagree with the registry.

When the module owns persistence, its debounced flash save runs on ZMK's
low-priority work queue rather than Zephyr's shared system work queue. A curve
edit therefore cannot hold up Bluetooth, split, watchdog, or device-PM work.

### Editing from a Studio client

`CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_CUSTOM_SETTINGS=y` registers the
four values in [zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings),
which is a registry shared across modules: a Studio client that renders it
renders these too, with the declared type and range driving the widget, so the
module needs no page of its own. The defaults are the devicetree values.
They appear under the `amgskobo__accel` subsystem. The subsystem is part of
every persisted settings name, so its length reduces the space available for
each node name.

A key is the owning node's devicetree name, then the field:

```
vector_accel.min_factor
vector_accel.max_factor
padstick_vector_accel.min_factor
padstick_vector_accel.max_factor
```

The node name is deliberate. A view drawing the pointer chain walks devicetree
for the processors in each listener and gets a `const struct device *` per
stage, whose `->name` is `DEVICE_DT_NAME()`, which is `DT_NODE_FULL_NAME()` —
the same string the key is built from. So a curve's settings are exactly the
keys starting with its device name, and nothing has to be registered, agreed
between modules, or typed into devicetree by a board author for that to hold.

It removes the commonest way to collide, since a name is no longer written by
hand — though not every way, because `DT_NODE_FULL_NAME` is a node's own name
and not its path, so devicetree keeps it unique only among siblings. The module
checks for a duplicate once at startup and logs it. Renaming a node orphans its
stored value. Both the 48-byte RPC key and the 64-byte persisted settings name
are checked at build time, so a too-long name fails rather than truncating. If
this integration is enabled, keep node names to 19 characters or fewer
(`pointer_accel`, `stick_accel`).

The option needs the patched ZMK that carries the custom Studio RPC protocol,
along with `zmk-feature-custom-settings` in `config/west.yml`. It is off by
default; an upstream ZMK build compiles none of it and, when `CONFIG_SETTINGS`
is enabled, the module uses its own store.

`ZMK_CUSTOM_SETTING_RANGE_INT32` repeats the devicetree `BUILD_ASSERT` bounds so
a client can refuse an impossible value before sending it. It cannot express
"max-speed must exceed unity-speed", so the four are applied as a set and
`vector_accel_config_valid()` has the last word: a combination it rejects leaves
the previous curve running rather than half-applying.

## Testing

The calculation and stream-state core is platform independent. Run its
optimized, sanitizer, and 32-bit contracts in the same container family as a
ZMK build:

```bash
bash ./tests/run-docker.sh
```

The test suite covers vector symmetry, curve limits and monotonicity,
fixed-point remainders, integer saturation, inactivity reset, one-report
timing, and isolation between input streams.

The integration suite builds the base driver against upstream ZMK, builds and
runs the optional custom-settings adapter against the DYA fork, exercises the
runtime API and persistence on `native_sim`, rejects invalid devicetree values,
and builds standalone and split firmware for an ARM board:

```bash
bash ./tests/run-integration-docker.sh
```

## License

[MIT](LICENSE)
