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

static void test_report_state(void) {
    struct vector_accel_stream stream;
    vector_accel_stream_init(&stream);

    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1000) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 100);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_Y, 1000) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, 10);
    vector_accel_stream_finish_frame(&stream, &test_config, 1000);
    assert(stream.factor == 1000U);

    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1010) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 100);
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_Y, 1010) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_Y, 10);
    vector_accel_stream_finish_frame(&stream, &test_config, 1010);
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

    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1201) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 10);
    vector_accel_stream_finish_frame(&stream, &test_config, 1201);
    assert(stream.factor == 1000U);
}

static void test_independent_streams(void) {
    struct vector_accel_stream first;
    struct vector_accel_stream second;
    vector_accel_stream_init(&first);
    vector_accel_stream_init(&second);

    vector_accel_stream_begin_axis(&first, VECTOR_ACCEL_AXIS_X, 10);
    vector_accel_stream_add(&first, VECTOR_ACCEL_AXIS_X, 100);
    vector_accel_stream_finish_frame(&first, &test_config, 10);
    vector_accel_stream_begin_axis(&first, VECTOR_ACCEL_AXIS_X, 20);
    vector_accel_stream_add(&first, VECTOR_ACCEL_AXIS_X, 100);
    vector_accel_stream_finish_frame(&first, &test_config, 20);

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

    /* A second X proves a new report started. The stale 1000 must be dropped. */
    assert(vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 30) == 1000U);
    vector_accel_stream_add(&stream, VECTOR_ACCEL_AXIS_X, 1);
    vector_accel_stream_finish_frame(&stream, &test_config, 30);
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

int main(void) {
    test_absolute_value();
    test_magnitude();
    test_curve();
    test_speed_equivalence();
    test_remainders_and_saturation();
    test_report_state();
    test_independent_streams();
    test_incomplete_frame_recovery();
    test_invalid_axis_is_ignored();
    test_config_validation();
    test_unusual_input_guards();
    puts("vector acceleration tests: PASS");
    return 0;
}
