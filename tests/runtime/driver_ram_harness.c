/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The build in which custom-settings owns persistence: the driver keeps its
 * values in RAM, schedules no save and defines no save work.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zmk-input-vector-acceleration/vector_accel_core.h>

struct k_spinlock { int held; };
struct device { const void *config; void *data; const char *name; };

#define IS_ENABLED(option) option
#define VECTOR_ACCEL_STREAM_COUNT 2
#define ARG_UNUSED(x) ((void)(x))

struct vector_accel_data {
    struct vector_accel_stream streams[VECTOR_ACCEL_STREAM_COUNT];
    int16_t fallback_remainders[VECTOR_ACCEL_STREAM_COUNT][VECTOR_ACCEL_AXIS_COUNT];
    struct k_spinlock config_lock;
    struct vector_accel_config config;
};

/* DRIVER_FUNCTIONS */

int main(void) {
    static const struct vector_accel_config defaults = {
        .min_factor = 500, .max_factor = 3000, .unity_speed = 100, .max_speed = 1000};
    struct vector_accel_data data;
    struct device dev = {.config = &defaults, .data = &data, .name = "ram"};

    memset(&data, 0x5a, sizeof(data));
    data.streams[0].frame_open = true;
    data.streams[1].frame_open = true;
    assert(vector_accel_init(&dev) == 0);
    assert(data.config.max_factor == 3000);
    assert(!data.streams[0].frame_open && !data.streams[1].frame_open);
    for (size_t i = 0; i < sizeof(data.fallback_remainders); i++) {
        assert(((const uint8_t *)data.fallback_remainders)[i] == 0x5a);
    }
    assert(vector_accel_schedule_save(&data) == 0);
    puts("vector-acceleration RAM-only driver: PASS");
    return 0;
}
