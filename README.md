# ZMK Input Vector Acceleration

`zmk-input-vector-acceleration` is a ZMK input processor for relative pointer
motion. It calculates one acceleration factor from the combined X/Y vector and
applies that same factor to both axes. Diagonal motion therefore keeps its
direction instead of being distorted by independent per-axis acceleration.

The implementation uses integer and fixed-point arithmetic only. State and
fractional remainders are independent for each input listener/device stream.
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

## Testing

The calculation and stream-state core is platform independent. On a system
with a C compiler:

```sh
./tests/run.sh
```

The test suite covers vector symmetry, curve limits and monotonicity,
fixed-point remainders, integer saturation, inactivity reset, one-report
timing, and isolation between input streams.

## License

MIT
