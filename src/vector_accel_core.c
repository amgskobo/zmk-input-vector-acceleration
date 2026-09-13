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
    int64_t sum = (int64_t)left + (int64_t)right;

    if (sum > INT32_MAX) {
        return INT32_MAX;
    }
    if (sum < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)sum;
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
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

uint32_t vector_accel_magnitude(int32_t x, int32_t y) {
    uint32_t abs_x = vector_accel_abs_i32(x);
    uint32_t abs_y = vector_accel_abs_i32(y);
    uint32_t major = abs_x > abs_y ? abs_x : abs_y;
    uint32_t minor = abs_x > abs_y ? abs_y : abs_x;
    uint64_t approximation = (uint64_t)major + (((uint64_t)minor * 3U) >> 3);

    return approximation > UINT32_MAX ? UINT32_MAX : (uint32_t)approximation;
}

uint32_t vector_accel_speed(uint32_t magnitude, uint32_t interval_ms) {
    if (interval_ms == 0U) {
        interval_ms = 1U;
    }

    uint64_t speed = ((uint64_t)magnitude * 1000U) / interval_ms;
    return speed > UINT32_MAX ? UINT32_MAX : (uint32_t)speed;
}

uint16_t vector_accel_compute_factor(const struct vector_accel_config *config, uint32_t speed) {
    const uint32_t min_factor = clamp_u32(config->min_factor, 100U, VECTOR_ACCEL_SCALE);
    const uint32_t max_factor = clamp_u32(config->max_factor, VECTOR_ACCEL_SCALE, 20000U);
    const uint32_t unity_speed = config->unity_speed == 0U ? 1U : config->unity_speed;
    const uint32_t max_speed = config->max_speed > unity_speed ? config->max_speed : unity_speed;

    if (speed <= unity_speed) {
        uint32_t position = (uint32_t)(((uint64_t)speed * VECTOR_ACCEL_SCALE) / unity_speed);
        uint32_t shaped = (uint32_t)(((uint64_t)position * position) / VECTOR_ACCEL_SCALE);
        uint32_t span = VECTOR_ACCEL_SCALE - min_factor;
        return (uint16_t)(min_factor + (uint32_t)(((uint64_t)span * shaped) / VECTOR_ACCEL_SCALE));
    }

    if (speed >= max_speed) {
        return (uint16_t)max_factor;
    }

    uint32_t position = (uint32_t)(((uint64_t)(speed - unity_speed) * VECTOR_ACCEL_SCALE) /
                                   (max_speed - unity_speed));
    uint32_t shaped = (uint32_t)(((uint64_t)position * position) / VECTOR_ACCEL_SCALE);
    uint32_t span = max_factor - VECTOR_ACCEL_SCALE;
    return (uint16_t)(VECTOR_ACCEL_SCALE +
                      (uint32_t)(((uint64_t)span * shaped) / VECTOR_ACCEL_SCALE));
}

int32_t vector_accel_scale_value(int32_t value, uint16_t factor, int32_t *remainder) {
    int32_t previous_remainder = remainder == NULL ? 0 : *remainder;
    int64_t total = (int64_t)value * factor + previous_remainder;
    int64_t output = total / VECTOR_ACCEL_SCALE;

    if (output > INT32_MAX) {
        if (remainder != NULL) {
            *remainder = 0;
        }
        return INT32_MAX;
    }
    if (output < INT32_MIN) {
        if (remainder != NULL) {
            *remainder = 0;
        }
        return INT32_MIN;
    }

    if (remainder != NULL) {
        *remainder = (int32_t)(total - output * VECTOR_ACCEL_SCALE);
    }
    return (int32_t)output;
}

void vector_accel_stream_init(struct vector_accel_stream *stream) {
    *stream = (struct vector_accel_stream){
        .factor = VECTOR_ACCEL_SCALE,
    };
}

uint16_t vector_accel_stream_begin_axis(struct vector_accel_stream *stream,
                                        enum vector_accel_axis axis, int64_t now_ms) {
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
    if (stream->frame_open && axis_bit != 0U && (stream->seen_axes & axis_bit) != 0U) {
        stream->frame_delta[VECTOR_ACCEL_AXIS_X] = 0;
        stream->frame_delta[VECTOR_ACCEL_AXIS_Y] = 0;
        stream->seen_axes = 0U;
        stream->frame_open = false;
    }

    if (!stream->frame_open) {
        stream->frame_open = true;

        if (!stream->have_report_time || now_ms <= stream->last_report_time_ms ||
            now_ms - stream->last_report_time_ms > VECTOR_ACCEL_HISTORY_TIMEOUT_MS) {
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
                                      const struct vector_accel_config *config, int64_t now_ms) {
    if (!stream->frame_open) {
        return;
    }

    if (stream->have_report_time && now_ms > stream->last_report_time_ms) {
        int64_t elapsed_ms = now_ms - stream->last_report_time_ms;

        if (elapsed_ms <= VECTOR_ACCEL_HISTORY_TIMEOUT_MS) {
            uint32_t magnitude = vector_accel_magnitude(stream->frame_delta[VECTOR_ACCEL_AXIS_X],
                                                        stream->frame_delta[VECTOR_ACCEL_AXIS_Y]);
            uint32_t speed = vector_accel_speed(magnitude, (uint32_t)elapsed_ms);
            stream->factor = vector_accel_compute_factor(config, speed);
        } else {
            stream->factor = VECTOR_ACCEL_SCALE;
        }
    } else {
        stream->factor = VECTOR_ACCEL_SCALE;
    }

    stream->frame_delta[VECTOR_ACCEL_AXIS_X] = 0;
    stream->frame_delta[VECTOR_ACCEL_AXIS_Y] = 0;
    stream->seen_axes = 0U;
    stream->last_report_time_ms = now_ms;
    stream->have_report_time = true;
    stream->frame_open = false;
}
