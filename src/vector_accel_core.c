/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <stddef.h>

#include <zmk-input-vector-acceleration/vector_accel_core.h>

static uint32_t clamp_u32(uint32_t value, uint32_t lower, uint32_t upper) {
    if (value < lower) {
        return lower;
    }
    if (value > upper) {
        return upper;
    }
    return value;
}

static int32_t saturating_add_i32(int32_t left, int32_t right) {
    int32_t sum;
    if (__builtin_add_overflow(left, right, &sum)) {
        return left < 0 ? INT32_MIN : INT32_MAX;
    }
    return sum;
}

static uint32_t saturating_add_u32(uint32_t left, uint32_t right) {
    return left > UINT32_MAX - right ? UINT32_MAX : left + right;
}

/* What a history sum keeps as the next frame joins it. */
static uint32_t history_decay(uint32_t sum) {
    return sum - (sum >> VECTOR_ACCEL_HISTORY_DECAY_SHIFT);
}

static uint32_t scale_ratio(uint32_t numerator, uint32_t denominator) {
    return vector_accel_speed(numerator, denominator);
}

bool vector_accel_config_valid(const struct vector_accel_config *config) {
    if (config == NULL) {
        return false;
    }

    if (config->min_factor < VECTOR_ACCEL_MIN_FACTOR_FLOOR ||
        config->min_factor > VECTOR_ACCEL_SCALE) {
        return false;
    }

    if (config->max_factor < VECTOR_ACCEL_SCALE ||
        config->max_factor > VECTOR_ACCEL_MAX_FACTOR_CEILING) {
        return false;
    }

    if (config->unity_speed == 0U) {
        return false;
    }

    return config->max_speed > config->unity_speed;
}

uint32_t vector_accel_abs_i32(int32_t value) {
    return value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
}

uint32_t vector_accel_magnitude(int32_t x, int32_t y) {
    uint32_t abs_x = vector_accel_abs_i32(x);
    uint32_t abs_y = vector_accel_abs_i32(y);
    uint32_t major = abs_x > abs_y ? abs_x : abs_y;
    uint32_t minor = abs_x > abs_y ? abs_y : abs_x;
    uint32_t approximation = major + (minor >> 3) * 3U + (((minor & 7U) * 3U) >> 3);

    /* |x| and |y| are at most 2^31, so this is at most 11/8 * 2^31. */
    return (uint32_t)approximation;
}

uint32_t vector_accel_speed(uint32_t magnitude, uint32_t interval_ms) {
    if (interval_ms == 0U) {
        interval_ms = 1U;
    }

    if (magnitude <= UINT32_MAX / VECTOR_ACCEL_SCALE) {
        return (magnitude * VECTOR_ACCEL_SCALE) / interval_ms;
    }

    /*
     * q * scale + (r * scale) / interval is exactly the same as
     * (magnitude * scale) / interval. When the interval is small enough, the
     * remainder product fits uint32_t and no software 64-bit division is
     * needed. Every interval accepted by the stream hot path is at most 100.
     */
    if (interval_ms <= UINT32_MAX / VECTOR_ACCEL_SCALE + 1U) {
        uint32_t quotient = magnitude / interval_ms;
        uint32_t remainder = magnitude - quotient * interval_ms;

        if (quotient > UINT32_MAX / VECTOR_ACCEL_SCALE) {
            return UINT32_MAX;
        }

        uint32_t scaled = quotient * VECTOR_ACCEL_SCALE;
        uint32_t fraction = (remainder * VECTOR_ACCEL_SCALE) / interval_ms;

        return fraction > UINT32_MAX - scaled ? UINT32_MAX : scaled + fraction;
    }

    /* Binary long multiplication with a normalized remainder. Every add
     * stays below the denominator; the quotient is at most SCALE - 1. */
    uint32_t quotient = magnitude / interval_ms;
    uint32_t source_remainder = magnitude % interval_ms;
    uint32_t fraction = 0U;
    uint32_t rest = 0U;
    for (uint32_t bit = 512U; bit != 0U; bit >>= 1) {
        fraction *= 2U;
        if (rest >= interval_ms - rest) {
            rest -= interval_ms - rest;
            fraction++;
        } else {
            rest += rest;
        }
        if ((VECTOR_ACCEL_SCALE & bit) != 0U) {
            if (rest >= interval_ms - source_remainder) {
                rest -= interval_ms - source_remainder;
                fraction++;
            } else {
                rest += source_remainder;
            }
        }
    }
    return quotient * VECTOR_ACCEL_SCALE + fraction;
}

uint16_t vector_accel_compute_factor(const struct vector_accel_config *config, uint32_t speed) {
    const uint32_t min_factor = clamp_u32(config->min_factor, 100U, VECTOR_ACCEL_SCALE);
    const uint32_t max_factor = clamp_u32(config->max_factor, VECTOR_ACCEL_SCALE, 20000U);
    const uint32_t unity_speed = config->unity_speed == 0U ? 1U : config->unity_speed;
    const uint32_t max_speed = config->max_speed > unity_speed ? config->max_speed : unity_speed;

    if (speed <= unity_speed) {
        uint32_t position = scale_ratio(speed, unity_speed);
        uint32_t shaped = (position * position) / VECTOR_ACCEL_SCALE;
        uint32_t span = VECTOR_ACCEL_SCALE - min_factor;
        return (uint16_t)(min_factor + (span * shaped) / VECTOR_ACCEL_SCALE);
    }

    if (speed >= max_speed) {
        return (uint16_t)max_factor;
    }

    uint32_t position = scale_ratio(speed - unity_speed, max_speed - unity_speed);
    uint32_t shaped = (position * position) / VECTOR_ACCEL_SCALE;
    uint32_t span = max_factor - VECTOR_ACCEL_SCALE;
    return (uint16_t)(VECTOR_ACCEL_SCALE + (span * shaped) / VECTOR_ACCEL_SCALE);
}

int32_t vector_accel_scale_value(int32_t value, uint16_t factor, int32_t *remainder) {
    const int32_t scale = (int32_t)VECTOR_ACCEL_SCALE;
    const int32_t previous = remainder == NULL ? 0 : *remainder;
    int32_t numerator;
    if (!__builtin_mul_overflow(value, (int32_t)factor, &numerator) &&
        !__builtin_add_overflow(numerator, previous, &numerator)) {
        if (remainder != NULL) {
            *remainder = numerator % scale;
        }
        return numerator / scale;
    }

    const int32_t whole = value / scale;
    const int32_t fraction = (value % scale) * (int32_t)factor + previous % scale;
    const int32_t correction = previous / scale + fraction / scale;
    int32_t rest = fraction % scale;
    bool negative = whole < 0;
    uint32_t magnitude;
    bool saturated = __builtin_mul_overflow(vector_accel_abs_i32(whole),
                                              (uint32_t)factor, &magnitude);
    if (!saturated) {
        uint32_t adjustment = vector_accel_abs_i32(correction);
        if ((correction < 0) == negative) {
            saturated = __builtin_add_overflow(magnitude, adjustment, &magnitude);
        } else if (adjustment > magnitude) {
            magnitude = adjustment - magnitude;
            negative = !negative;
        } else {
            magnitude -= adjustment;
        }
    }

    if (!saturated && magnitude > 0U) {
        if (!negative && rest < 0) {
            magnitude--;
            rest += scale;
        } else if (negative && rest > 0) {
            magnitude--;
            rest -= scale;
        }
    }
    const uint32_t limit = negative ? (uint32_t)INT32_MAX + 1U : (uint32_t)INT32_MAX;
    if (saturated || magnitude > limit) {
        if (remainder != NULL) {
            *remainder = 0;
        }
        return negative ? INT32_MIN : INT32_MAX;
    }
    if (remainder != NULL) {
        *remainder = rest;
    }
    if (negative && magnitude == (uint32_t)INT32_MAX + 1U) {
        return INT32_MIN;
    }
    return negative ? -(int32_t)magnitude : (int32_t)magnitude;
}

void vector_accel_stream_init(struct vector_accel_stream *stream) {
    *stream = (struct vector_accel_stream){
        .factor = VECTOR_ACCEL_SCALE,
    };
}

static bool recent_report_interval(const struct vector_accel_stream *stream,
                                   uint32_t now_ms, uint32_t *interval_ms) {
    if (!stream->have_report_time) {
        return false;
    }

    /* A stroke spans at most HISTORY_TIMEOUT_MS. Unsigned subtraction
     * preserves it across uptime wrap; a full 2^32 ms silent span is outside
     * this clock contract. */
    uint32_t elapsed = now_ms - stream->last_report_time_ms;
    if (elapsed > VECTOR_ACCEL_HISTORY_TIMEOUT_MS) {
        return false;
    }
    if (interval_ms != NULL) {
        *interval_ms = (uint32_t)elapsed;
    }
    return true;
}

uint16_t vector_accel_stream_begin_axis(struct vector_accel_stream *stream,
                                        enum vector_accel_axis axis, uint32_t now_ms) {
    if ((uint32_t)axis >= VECTOR_ACCEL_AXIS_COUNT) {
        return stream->factor == 0U ? VECTOR_ACCEL_SCALE : stream->factor;
    }

    uint8_t axis_bit = (uint8_t)(1U << axis);

    /*
     * A layer override is selected separately for every ZMK input event. If a
     * layer changes between X and the synchronized Y event, this processor can
     * see X but miss the sync that closes its frame. Seeing the same axis again
     * proves that the old report is incomplete, so discard it before it can be
     * combined with the new report.
     */
    /* The invalid-axis guard above guarantees axis_bit is either 1 or 2. */
    if (stream->frame_open && (stream->seen_axes & axis_bit) != 0U) {
        stream->frame_delta[VECTOR_ACCEL_AXIS_X] = 0;
        stream->frame_delta[VECTOR_ACCEL_AXIS_Y] = 0;
        stream->seen_axes = 0U;
        stream->frame_open = false;
    }

    if (!stream->frame_open) {
        stream->frame_open = true;

        /* A frame in the same millisecond as the last one is part of a burst,
         * not the start of a new stroke: it keeps the factor. */
        if (!recent_report_interval(stream, now_ms, NULL)) {
            stream->factor = VECTOR_ACCEL_SCALE;
        }
    }

    stream->seen_axes |= axis_bit;

    return stream->factor == 0U ? VECTOR_ACCEL_SCALE : stream->factor;
}

void vector_accel_stream_add(struct vector_accel_stream *stream, enum vector_accel_axis axis,
                             int32_t value) {
    if ((uint32_t)axis >= VECTOR_ACCEL_AXIS_COUNT) {
        return;
    }

    stream->frame_delta[axis] = saturating_add_i32(stream->frame_delta[axis], value);
}

void vector_accel_stream_finish_frame(struct vector_accel_stream *stream,
                                      const struct vector_accel_config *config, uint32_t now_ms) {
    if (!stream->frame_open) {
        return;
    }

    uint32_t interval_ms;
    if (recent_report_interval(stream, now_ms, &interval_ms)) {
        uint32_t magnitude = vector_accel_magnitude(stream->frame_delta[VECTOR_ACCEL_AXIS_X],
                                                    stream->frame_delta[VECTOR_ACCEL_AXIS_Y]);

        stream->history_magnitude =
            saturating_add_u32(history_decay(stream->history_magnitude), magnitude);
        stream->history_span_ms = history_decay(stream->history_span_ms) + interval_ms;
        stream->factor = vector_accel_compute_factor(
            config, vector_accel_speed(stream->history_magnitude, stream->history_span_ms));
    } else {
        /* No history, or too old to describe this stroke: start it at unity. */
        stream->history_magnitude = 0U;
        stream->history_span_ms = 0U;
        stream->factor = VECTOR_ACCEL_SCALE;
    }

    stream->frame_delta[VECTOR_ACCEL_AXIS_X] = 0;
    stream->frame_delta[VECTOR_ACCEL_AXIS_Y] = 0;
    stream->seen_axes = 0U;
    stream->last_report_time_ms = now_ms;
    stream->have_report_time = true;
    stream->frame_open = false;
}
