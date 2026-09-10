/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <zmk-input-vector-acceleration/vector_accel_core.h>

static const struct vector_accel_config test_config = {
    .min_factor = 500,
    .max_factor = 3200,
    .unity_speed = 1200,
    .max_speed = 6000,
};

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
        assert(factor >= previous);
        assert(factor >= 500U && factor <= 3200U);
        previous = factor;
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
    uint16_t factor =
        vector_accel_stream_begin_axis(&stream, VECTOR_ACCEL_AXIS_X, 1020);
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

    enum vector_accel_axis invalid = (enum vector_accel_axis)-1;
    assert(vector_accel_stream_begin_axis(&stream, invalid, 10) == 1000U);
    vector_accel_stream_add(&stream, invalid, 100);
    assert(!stream.frame_open);
    assert(stream.frame_delta[VECTOR_ACCEL_AXIS_X] == 0);
    assert(stream.frame_delta[VECTOR_ACCEL_AXIS_Y] == 0);
}

int main(void) {
    test_absolute_value();
    test_magnitude();
    test_curve();
    test_remainders_and_saturation();
    test_report_state();
    test_independent_streams();
    test_incomplete_frame_recovery();
    test_invalid_axis_is_ignored();
    puts("vector acceleration tests: PASS");
    return 0;
}
