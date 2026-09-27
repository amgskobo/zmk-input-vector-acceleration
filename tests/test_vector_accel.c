/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <zmk-input-vector-acceleration/vector_accel_core.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static const struct vector_accel_config test_config = {
    .min_factor = 500,
    .max_factor = 3200,
    .unity_speed = 1200,
    .max_speed = 6000,
};

static uint32_t reference_speed(uint32_t magnitude, uint32_t interval_ms) {
    if (interval_ms == 0U) {
        interval_ms = 1U;
    }

    uint64_t speed = ((uint64_t)magnitude * VECTOR_ACCEL_SCALE) / interval_ms;
    return speed > UINT32_MAX ? UINT32_MAX : (uint32_t)speed;
}

static uint16_t reference_factor(const struct vector_accel_config *config, uint32_t speed) {
    uint32_t min_factor = config->min_factor < VECTOR_ACCEL_MIN_FACTOR_FLOOR
                              ? VECTOR_ACCEL_MIN_FACTOR_FLOOR
                              : config->min_factor;
    uint32_t max_factor = config->max_factor > VECTOR_ACCEL_MAX_FACTOR_CEILING
                              ? VECTOR_ACCEL_MAX_FACTOR_CEILING
                              : config->max_factor;
    uint32_t unity_speed = config->unity_speed == 0U ? 1U : config->unity_speed;
    uint32_t max_speed = config->max_speed > unity_speed ? config->max_speed : unity_speed;

    if (min_factor > VECTOR_ACCEL_SCALE) {
        min_factor = VECTOR_ACCEL_SCALE;
    }
    if (max_factor < VECTOR_ACCEL_SCALE) {
        max_factor = VECTOR_ACCEL_SCALE;
    }

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

static int32_t reference_scale_value(int32_t value, uint16_t factor, int32_t *remainder) {
    int64_t total = (int64_t)value * factor + *remainder;
    int64_t output = total / VECTOR_ACCEL_SCALE;

    if (output > INT32_MAX) {
        *remainder = 0;
        return INT32_MAX;
    }
    if (output < INT32_MIN) {
        *remainder = 0;
        return INT32_MIN;
    }

    *remainder = (int32_t)(total - output * VECTOR_ACCEL_SCALE);
    return (int32_t)output;
}

static void test_absolute_value(void) {
    assert(vector_accel_abs_i32(0) == 0U);
    assert(vector_accel_abs_i32(-123) == 123U);
    assert(vector_accel_abs_i32(INT32_MIN) == 2147483648U);
}

static void test_magnitude(void) {
    assert(vector_accel_magnitude(100, 0) == 100U);
    assert(vector_accel_magnitude(0, -100) == 100U);
    assert(vector_accel_magnitude(100, 100) == 137U);
    assert(vector_accel_magnitude(100, 25) == vector_accel_magnitude(25, 100));
    assert(vector_accel_magnitude(-100, 25) == vector_accel_magnitude(100, -25));
}

static void test_curve(void) {
    assert(vector_accel_compute_factor(&test_config, 0) == 500U);
    assert(vector_accel_compute_factor(&test_config, 1200) == 1000U);
    assert(vector_accel_compute_factor(&test_config, 6000) == 3200U);
    assert(vector_accel_compute_factor(&test_config, UINT32_MAX) == 3200U);

    uint16_t previous = 0U;
    for (uint32_t speed = 0U; speed <= 7000U; speed++) {
        uint16_t factor = vector_accel_compute_factor(&test_config, speed);
        assert(factor == reference_factor(&test_config, speed));
        assert(factor >= previous);
        assert(factor >= 500U && factor <= 3200U);
        previous = factor;
    }

    static const struct vector_accel_config extreme_configs[] = {
        {.min_factor = 100,
         .max_factor = 20000,
         .unity_speed = UINT32_MAX - 1U,
         .max_speed = UINT32_MAX},
        {.min_factor = 1000, .max_factor = 1000, .unity_speed = 1, .max_speed = UINT32_MAX},
    };
    static const uint32_t extreme_speeds[] = {
        0U,
        1U,
        UINT32_MAX / VECTOR_ACCEL_SCALE,
        UINT32_MAX / VECTOR_ACCEL_SCALE + 1U,
        UINT32_MAX / 2U,
        UINT32_MAX - 1U,
        UINT32_MAX,
    };

    for (size_t config_index = 0U; config_index < ARRAY_SIZE(extreme_configs); config_index++) {
        for (size_t speed_index = 0U; speed_index < ARRAY_SIZE(extreme_speeds); speed_index++) {
            assert(vector_accel_compute_factor(&extreme_configs[config_index],
                                               extreme_speeds[speed_index]) ==
                   reference_factor(&extreme_configs[config_index], extreme_speeds[speed_index]));
        }
    }
}

static void test_speed_equivalence(void) {
    static const uint32_t magnitudes[] = {
        0U,
        1U,
        999U,
        1000U,
        UINT32_MAX / VECTOR_ACCEL_SCALE,
        UINT32_MAX / VECTOR_ACCEL_SCALE + 1U,
        UINT32_MAX - 1U,
        UINT32_MAX,
    };
    static const uint32_t intervals[] = {
        0U,
        1U,
        2U,
        100U,
        UINT32_MAX / VECTOR_ACCEL_SCALE,
        UINT32_MAX / VECTOR_ACCEL_SCALE + 1U,
        UINT32_MAX / VECTOR_ACCEL_SCALE + 2U,
        UINT32_MAX,
    };

    for (size_t magnitude_index = 0U; magnitude_index < ARRAY_SIZE(magnitudes); magnitude_index++) {
        for (size_t interval_index = 0U; interval_index < ARRAY_SIZE(intervals); interval_index++) {
            assert(vector_accel_speed(magnitudes[magnitude_index], intervals[interval_index]) ==
                   reference_speed(magnitudes[magnitude_index], intervals[interval_index]));
        }
    }
}

static void test_remainders_and_saturation(void) {
    int32_t remainder = 0;
    assert(vector_accel_scale_value(1, 500, &remainder) == 0);
    assert(remainder == 500);
    assert(vector_accel_scale_value(1, 500, &remainder) == 1);
    assert(remainder == 0);

    assert(vector_accel_scale_value(-1, 500, &remainder) == 0);
    assert(remainder == -500);
    assert(vector_accel_scale_value(-1, 500, &remainder) == -1);
    assert(remainder == 0);

    assert(vector_accel_scale_value(INT32_MAX, 20000, &remainder) == INT32_MAX);
    assert(remainder == 0);
    assert(vector_accel_scale_value(INT32_MIN, 20000, &remainder) == INT32_MIN);
    assert(remainder == 0);

    static const int32_t values[] = {
        INT32_MIN, INT32_MIN + 1, -1001, -1000, -999,          -1,        0,
        1,         999,           1000,  1001,  INT32_MAX - 1, INT32_MAX,
    };
    static const uint16_t factors[] = {0U, 1U, 100U, 999U, 1000U, 20000U, UINT16_MAX};
    static const int32_t previous_remainders[] = {
        INT32_MIN, -1001, -1000, -999, -1, 0, 1, 999, 1000, 1001, INT32_MAX,
    };

    for (size_t value_index = 0U; value_index < ARRAY_SIZE(values); value_index++) {
        for (size_t factor_index = 0U; factor_index < ARRAY_SIZE(factors); factor_index++) {
            for (size_t remainder_index = 0U; remainder_index < ARRAY_SIZE(previous_remainders);
                 remainder_index++) {
                int32_t expected_remainder = previous_remainders[remainder_index];
                int32_t actual_remainder = expected_remainder;
                int32_t expected = reference_scale_value(values[value_index], factors[factor_index],
                                                         &expected_remainder);
                int32_t actual = vector_accel_scale_value(values[value_index],
                                                          factors[factor_index], &actual_remainder);

                assert(actual == expected);
                assert(actual_remainder == expected_remainder);
            }

            int32_t zero_remainder = 0;
            int32_t expected =
                reference_scale_value(values[value_index], factors[factor_index], &zero_remainder);
            assert(vector_accel_scale_value(values[value_index], factors[factor_index], NULL) ==
                   expected);
        }
    }
}

/* One X-only frame of travel x at time t; returns the factor it was scaled by. */
static uint16_t x_frame(struct vector_accel_stream *stream, int64_t t, int32_t x) {
    uint16_t factor = vector_accel_stream_begin_axis(stream, VECTOR_ACCEL_AXIS_X, t);

    vector_accel_stream_add(stream, VECTOR_ACCEL_AXIS_X, x);
    vector_accel_stream_finish_frame(stream, &test_config, t);
    return factor;
}

static void test_report_state(void) {
    struct vector_accel_stream stream;
    vector_accel_stream_init(&stream);

    /* The first frame has no interval: unity. */
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1000) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 100);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_Y, 1000) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, 10);
    vector_accel_stream_finish_frame(&stream, &test_config, 1000);
    assert(stream.factor == 1000U);
    assert(stream.history_magnitude == 0U && stream.history_span_ms == 0U);

    /* The second one's interval sets it: 103 counts in 10 ms, past max-speed. */
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1010) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 100);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_Y, 1010) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, 10);
    vector_accel_stream_finish_frame(&stream, &test_config, 1010);
    assert(stream.history_magnitude == 103U && stream.history_span_ms == 10U);
    assert(stream.factor == 3200U);

    int32_t rem_x = 0;
    int32_t rem_y = 0;
    uint16_t factor = vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1020);
    assert(factor == 3200U);
    assert(vector_accel_scale_value(100, factor, &rem_x) == 320);
    assert(vector_accel_scale_value(10, factor, &rem_y) == 32);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 100);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_Y, 1020) == 3200U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, 10);
    vector_accel_stream_finish_frame(&stream, &test_config, 1020);
    /* Each sum keeps half: 103 - 51 + 103 counts in 10 - 5 + 10 ms. */
    assert(stream.history_magnitude == 155U && stream.history_span_ms == 15U);

    /* A pause past the history timeout starts over, history and all. */
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1201) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 10);
    vector_accel_stream_finish_frame(&stream, &test_config, 1201);
    assert(stream.factor == 1000U);
    assert(stream.history_magnitude == 0U && stream.history_span_ms == 0U);
}

/*
 * A steady 3000 counts/s - 30 counts every 10 ms - as a split peripheral's
 * frames reached the central in a simulation: at 7.5 ms connection events,
 * one frame late and another straight after it. Each frame's own interval
 * reads from 1363 to 30000 counts/s; the history keeps every factor to what
 * 1900 to 3800 counts/s would give.
 */
static void test_jittered_arrivals(void) {
    static const int64_t arrivals_us[] = {
        4005737, 4013214, 4028228, 4035736, 4043212, 4058227, 4065734, 4073211, 4088226,
        4095733, 4103240, 4125732, 4126647, 4133239, 4148223, 4155731, 4163238, 4178222,
        4185729, 4193237, 4208221, 4215728, 4223236, 4238220, 4245727, 4253234, 4275726,
        4276641, 4283233, 4298217, 4305725, 4313232, 4328216, 4335723, 4343231, 4358215,
    };
    const uint16_t low = vector_accel_compute_factor(&test_config, 1900U);
    const uint16_t high = vector_accel_compute_factor(&test_config, 3800U);
    struct vector_accel_stream stream;

    vector_accel_stream_init(&stream);
    x_frame(&stream, arrivals_us[0] / 1000, 30);
    for (size_t i = 1U; i < sizeof(arrivals_us) / sizeof(arrivals_us[0]); i++) {
        x_frame(&stream, arrivals_us[i] / 1000, 30);
        assert(stream.factor >= low && stream.factor <= high);
    }
}

static void test_independent_streams(void) {
    struct vector_accel_stream first;
    struct vector_accel_stream second;
    vector_accel_stream_init(&first);
    vector_accel_stream_init(&second);

    x_frame(&first, 10, 100);
    x_frame(&first, 20, 100);

    assert(first.factor == 3200U);
    assert(vector_accel_stream_begin_axis(&second, VECTOR_ACCEL_AXIS_X, 20) == 1000U);
}

static void test_incomplete_frame_recovery(void) {
    struct vector_accel_stream stream;
    vector_accel_stream_init(&stream);

    vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 10);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 10);
    vector_accel_stream_finish_frame(&stream, &test_config, 10);

    /* The synchronized Y event is routed to another layer and never arrives. */
    vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 20);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 1000);

    /* A second X proves a new report started. The stale 1000 must be dropped:
     * it would have made the next frame's history fast. */
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 30) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 1);
    vector_accel_stream_finish_frame(&stream, &test_config, 30);
    assert(stream.history_magnitude == 1U);
    assert(stream.factor < 1000U);

    /* Recovery also applies the normal inactivity reset to the new report. */
    vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 40);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 1000);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 200) == 1000U);
}

static void test_invalid_axis_is_ignored(void) {
    struct vector_accel_stream stream;
    vector_accel_stream_init(&stream);

    enum vector_accel_axis invalid = (enum vector_accel_axis) - 1;
    assert(vector_accel_stream_begin_axis(&stream, invalid, 10) == 1000U);
    vector_accel_stream_add(&stream, invalid, 100);
    assert(!stream.frame_open);
    assert(stream.frame_delta[VECTOR_ACCEL_AXIS_X] == 0);
    assert(stream.frame_delta[VECTOR_ACCEL_AXIS_Y] == 0);
}

/*
 * The devicetree BUILD_ASSERTs cannot police a value that arrives at runtime,
 * so the setter leans on this function instead. Every bound it rejects here is
 * one the build would have rejected.
 */
static void test_config_validation(void) {
    const struct vector_accel_config good = {
        .min_factor = 500,
        .max_factor = 3200,
        .unity_speed = 1200,
        .max_speed = 6000,
    };
    struct vector_accel_config probe;

    assert(vector_accel_config_valid(&good));
    assert(!vector_accel_config_valid(NULL));

    probe = good;
    probe.min_factor = VECTOR_ACCEL_MIN_FACTOR_FLOOR - 1U;
    assert(!vector_accel_config_valid(&probe));

    probe = good;
    probe.min_factor = VECTOR_ACCEL_SCALE + 1U;
    assert(!vector_accel_config_valid(&probe));

    probe = good;
    probe.max_factor = VECTOR_ACCEL_SCALE - 1U;
    assert(!vector_accel_config_valid(&probe));

    probe = good;
    probe.max_factor = VECTOR_ACCEL_MAX_FACTOR_CEILING + 1U;
    assert(!vector_accel_config_valid(&probe));

    probe = good;
    probe.unity_speed = 0U;
    assert(!vector_accel_config_valid(&probe));

    /* max-speed must stay above unity-speed, including when they are equal. */
    probe = good;
    probe.max_speed = probe.unity_speed;
    assert(!vector_accel_config_valid(&probe));

    /* The extremes the build would accept have to remain acceptable. */
    probe = good;
    probe.min_factor = VECTOR_ACCEL_MIN_FACTOR_FLOOR;
    probe.max_factor = VECTOR_ACCEL_MAX_FACTOR_CEILING;
    assert(vector_accel_config_valid(&probe));
}

static void test_unusual_input_guards(void) {
    assert(vector_accel_speed(8589935U, 2U) == UINT32_MAX);
    struct vector_accel_config config = test_config;
    config.min_factor = 0U;
    config.max_factor = UINT16_MAX;
    assert(vector_accel_compute_factor(&config, 0U) == VECTOR_ACCEL_MIN_FACTOR_FLOOR);
    assert(vector_accel_compute_factor(&config, UINT32_MAX) == VECTOR_ACCEL_MAX_FACTOR_CEILING);
    config.unity_speed = 0U;
    assert(vector_accel_compute_factor(&config, 0U) == VECTOR_ACCEL_MIN_FACTOR_FLOOR);

    struct vector_accel_stream stream;
    vector_accel_stream_init(&stream);
    stream.factor = 0U;
    assert(vector_accel_stream_begin_axis(&stream, (enum vector_accel_axis)-1, 0) == 1000U);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 0) == 1000U);
    stream.factor = 0U;
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_Y, 0) == 1000U);
    vector_accel_stream_finish_frame(&stream, &test_config, 0);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 0) == 1000U);
    vector_accel_stream_finish_frame(&stream, &test_config, 0);
    vector_accel_stream_finish_frame(&stream, &test_config, 10);
    assert(stream.have_report_time && !stream.frame_open);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, INT32_MAX);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 1);
    assert(stream.frame_delta[VECTOR_ACCEL_AXIS_X] == INT32_MAX);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, INT32_MIN);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, -1);
    assert(stream.frame_delta[VECTOR_ACCEL_AXIS_Y] == INT32_MIN);
}

/* Every limit exactly, where the mutation tests found nothing looking. */
static void test_exact_boundaries(void) {
    /* A curve that never slows down, or never speeds up, is still a curve. */
    struct vector_accel_config probe = test_config;
    probe.min_factor = VECTOR_ACCEL_SCALE;
    assert(vector_accel_config_valid(&probe));
    probe = test_config;
    probe.max_factor = VECTOR_ACCEL_SCALE;
    assert(vector_accel_config_valid(&probe));

    /* A quotient exactly at the limit still fits; only past it saturates. */
    assert(vector_accel_speed(8589934U, 2U) == 4294967000U);

    /* The first axis past the last is as invalid as any other, and writes
     * nothing: not a frame bit, not the neighbouring timestamp. */
    struct vector_accel_stream stream;
    vector_accel_stream_init(&stream);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_COUNT, 10) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_COUNT, 100);
    assert(!stream.frame_open && stream.seen_axes == 0U && stream.last_report_time_ms == 0);

    /* Build up a fast factor: 100 - 50 + 100 = 150 counts in 10 - 5 + 10 =
     * 15 ms. */
    vector_accel_stream_init(&stream);
    x_frame(&stream, 1000, 100);
    x_frame(&stream, 1010, 100);
    x_frame(&stream, 1020, 100);
    assert(stream.history_magnitude == 150U && stream.history_span_ms == 15U);
    assert(stream.factor == 3200U);

    /* The same millisecond as the last report is a burst, not a new stroke:
     * the factor carries into the frame, and the frame joins the history
     * with no time of its own: 150 - 75 + 100 = 175 counts in 15 - 7 = 8 ms. */
    assert(x_frame(&stream, 1020, 100) == 3200U);
    assert(stream.history_magnitude == 175U && stream.history_span_ms == 8U);
    assert(stream.factor == 3200U);

    /* Exactly the history timeout later still counts as history: the factor
     * carries into the frame, and the frame joins the history. 175 - 87 + 100
     * = 188 counts in 8 - 4 + 100 = 104 ms is 1807 counts/s: position 126,
     * shaped 15, so 1000 + 2200 * 15 / 1000. */
    assert(x_frame(&stream, 1020 + VECTOR_ACCEL_HISTORY_TIMEOUT_MS, 100) == 3200U);
    assert(stream.history_magnitude == 188U && stream.history_span_ms == 104U);
    assert(stream.factor == 1033U);

    /* The history saturates rather than wraps. */
    stream.history_magnitude = UINT32_MAX;
    vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1130);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, INT32_MAX);
    vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_Y, 1130);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, INT32_MAX);
    vector_accel_stream_finish_frame(&stream, &test_config, 1130);
    assert(stream.history_magnitude == UINT32_MAX);
    assert(stream.factor == 3200U);
    /* A finished frame leaves nothing behind for the next one, on either axis. */
    assert(stream.frame_delta[VECTOR_ACCEL_AXIS_X] == 0 &&
           stream.frame_delta[VECTOR_ACCEL_AXIS_Y] == 0 && stream.seen_axes == 0U);
    struct vector_accel_stream both;
    vector_accel_stream_init(&both);
    vector_accel_stream_begin_axis(&both, VECTOR_ACCEL_AXIS_X, 10);
    vector_accel_stream_add(&both, VECTOR_ACCEL_AXIS_X, 3);
    vector_accel_stream_begin_axis(&both, VECTOR_ACCEL_AXIS_Y, 10);
    vector_accel_stream_add(&both, VECTOR_ACCEL_AXIS_Y, 5);
    vector_accel_stream_finish_frame(&both, &test_config, 10);
    assert(both.frame_delta[VECTOR_ACCEL_AXIS_X] == 0 && both.frame_delta[VECTOR_ACCEL_AXIS_Y] == 0);

    /* A repeated axis, either one, throws the open frame away and starts
     * over with only that axis seen. */
    const int64_t t = 1020 + VECTOR_ACCEL_HISTORY_TIMEOUT_MS + 10;
    for (int axis = VECTOR_ACCEL_AXIS_X; axis <= VECTOR_ACCEL_AXIS_Y; axis++) {
        const enum vector_accel_axis repeated = (enum vector_accel_axis)axis;

        vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, t);
        vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 7);
        vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_Y, t);
        vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, 9);
        assert(stream.seen_axes == 3U);
        vector_accel_stream_begin_axis(&stream, repeated, t);
        assert(stream.frame_open && stream.seen_axes == (1U << axis));
        assert(stream.frame_delta[VECTOR_ACCEL_AXIS_X] == 0 &&
               stream.frame_delta[VECTOR_ACCEL_AXIS_Y] == 0);
        vector_accel_stream_finish_frame(&stream, &test_config, t);
    }

    /* A frame that opens in time but closes past the history timeout has no
     * speed to measure: unity. */
    vector_accel_stream_init(&stream);
    x_frame(&stream, 1000, 100);
    x_frame(&stream, 1010, 100);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1060) == 3200U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 100);
    vector_accel_stream_finish_frame(&stream, &test_config, 1010 + VECTOR_ACCEL_HISTORY_TIMEOUT_MS + 1);
    assert(stream.factor == 1000U && stream.history_magnitude == 0U);

    /* A clock that runs backwards is no history either. */
    x_frame(&stream, 1300, 100);
    x_frame(&stream, 1310, 100);
    x_frame(&stream, 1320, 100);
    assert(stream.factor == 3200U);
    vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1320);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 100);
    vector_accel_stream_finish_frame(&stream, &test_config, 1319);
    assert(stream.factor == 1000U && stream.history_magnitude == 0U);
    assert(x_frame(&stream, 1100, 100) == 1000U);
}

int main(void) {
    test_absolute_value();
    test_magnitude();
    test_curve();
    test_speed_equivalence();
    test_remainders_and_saturation();
    test_report_state();
    test_jittered_arrivals();
    test_independent_streams();
    test_incomplete_frame_recovery();
    test_invalid_axis_is_ignored();
    test_config_validation();
    test_unusual_input_guards();
    test_exact_boundaries();
    puts("vector acceleration tests: PASS");
    return 0;
}
