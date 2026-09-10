/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VECTOR_ACCEL_SCALE 1000U
#define VECTOR_ACCEL_HISTORY_TIMEOUT_MS 100

enum vector_accel_axis {
    VECTOR_ACCEL_AXIS_X = 0,
    VECTOR_ACCEL_AXIS_Y = 1,
    VECTOR_ACCEL_AXIS_COUNT = 2,
};

struct vector_accel_config {
    uint16_t min_factor;
    uint16_t max_factor;
    uint32_t unity_speed;
    uint32_t max_speed;
};

struct vector_accel_stream {
    int32_t frame_delta[VECTOR_ACCEL_AXIS_COUNT];
    int64_t last_report_time_ms;
    uint16_t factor;
    uint8_t seen_axes;
    bool have_report_time;
    bool frame_open;
};

uint32_t vector_accel_abs_i32(int32_t value);
uint32_t vector_accel_magnitude(int32_t x, int32_t y);
uint32_t vector_accel_speed(uint32_t magnitude, uint32_t interval_ms);
uint16_t vector_accel_compute_factor(const struct vector_accel_config *config, uint32_t speed);
int32_t vector_accel_scale_value(int32_t value, uint16_t factor, int32_t *remainder);

void vector_accel_stream_init(struct vector_accel_stream *stream);
uint16_t vector_accel_stream_begin_axis(struct vector_accel_stream *stream,
                                        enum vector_accel_axis axis, int64_t now_ms);
void vector_accel_stream_add(struct vector_accel_stream *stream, enum vector_accel_axis axis,
                             int32_t value);
void vector_accel_stream_finish_frame(struct vector_accel_stream *stream,
                                      const struct vector_accel_config *config, int64_t now_ms);
