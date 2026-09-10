/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_vector_acceleration

#include <limits.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>

#include <zmk-input-vector-acceleration/vector_accel_core.h>

#define VECTOR_ACCEL_LISTENER_COUNT DT_NUM_INST_STATUS_OKAY(zmk_input_listener)
#define VECTOR_ACCEL_STREAM_COUNT MAX(VECTOR_ACCEL_LISTENER_COUNT, 1)

struct vector_accel_data {
    struct vector_accel_stream streams[VECTOR_ACCEL_STREAM_COUNT];
    int16_t fallback_remainders[VECTOR_ACCEL_STREAM_COUNT][VECTOR_ACCEL_AXIS_COUNT];
};

static struct vector_accel_stream *stream_for_event(struct vector_accel_data *data,
                                                    struct zmk_input_processor_state *state,
                                                    size_t *stream_index) {
    size_t index = 0U;

    if (state != NULL && state->input_device_index < VECTOR_ACCEL_STREAM_COUNT) {
        index = state->input_device_index;
    }

    if (stream_index != NULL) {
        *stream_index = index;
    }
    return &data->streams[index];
}

static int scale_axis_event(struct input_event *event, struct vector_accel_data *data,
                            struct vector_accel_stream *stream,
                            struct zmk_input_processor_state *state, size_t stream_index,
                            enum vector_accel_axis axis, int64_t now_ms) {
    int32_t raw = event->value;
    uint16_t factor = vector_accel_stream_begin_axis(stream, axis, now_ms);
    int32_t remainder;

    vector_accel_stream_add(stream, axis, raw);

    if (state != NULL && state->remainder != NULL) {
        remainder = *state->remainder;
        event->value = vector_accel_scale_value(raw, factor, &remainder);
        __ASSERT(remainder >= INT16_MIN && remainder <= INT16_MAX,
                 "vector acceleration remainder out of range");
        *state->remainder = (int16_t)remainder;
    } else {
        remainder = data->fallback_remainders[stream_index][axis];
        event->value = vector_accel_scale_value(raw, factor, &remainder);
        __ASSERT(remainder >= INT16_MIN && remainder <= INT16_MAX,
                 "vector acceleration fallback remainder out of range");
        data->fallback_remainders[stream_index][axis] = (int16_t)remainder;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int vector_accel_handle_event(const struct device *dev, struct input_event *event,
                                     uint32_t param1, uint32_t param2,
                                     struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    const struct vector_accel_config *config = dev->config;
    struct vector_accel_data *data = dev->data;
    size_t stream_index;
    struct vector_accel_stream *stream = stream_for_event(data, state, &stream_index);
    bool is_x = event->type == INPUT_EV_REL && event->code == INPUT_REL_X;
    bool is_y = event->type == INPUT_EV_REL && event->code == INPUT_REL_Y;
    int64_t now_ms = 0;

    if (is_x || is_y || (event->sync && stream->frame_open)) {
        now_ms = k_uptime_get();
    }

    if (is_x) {
        scale_axis_event(event, data, stream, state, stream_index, VECTOR_ACCEL_AXIS_X, now_ms);
    } else if (is_y) {
        scale_axis_event(event, data, stream, state, stream_index, VECTOR_ACCEL_AXIS_Y, now_ms);
    }

    if (event->sync && stream->frame_open) {
        vector_accel_stream_finish_frame(stream, config, now_ms);
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int vector_accel_init(const struct device *dev) {
    struct vector_accel_data *data = dev->data;

    for (size_t i = 0U; i < VECTOR_ACCEL_STREAM_COUNT; i++) {
        vector_accel_stream_init(&data->streams[i]);
    }

    return 0;
}

static const struct zmk_input_processor_driver_api vector_accel_api = {
    .handle_event = vector_accel_handle_event,
};

#define VECTOR_ACCEL_INST(n)                                                                  \
    BUILD_ASSERT(DT_INST_PROP(n, min_factor) >= 100 &&                                        \
                     DT_INST_PROP(n, min_factor) <= VECTOR_ACCEL_SCALE,                        \
                 "min-factor must be between 100 and 1000");                                 \
    BUILD_ASSERT(DT_INST_PROP(n, max_factor) >= VECTOR_ACCEL_SCALE &&                         \
                     DT_INST_PROP(n, max_factor) <= 20000,                                    \
                 "max-factor must be between 1000 and 20000");                               \
    BUILD_ASSERT(DT_INST_PROP(n, unity_speed) > 0, "unity-speed must be positive");          \
    BUILD_ASSERT(DT_INST_PROP(n, max_speed) > DT_INST_PROP(n, unity_speed),                   \
                 "max-speed must be greater than unity-speed");                              \
    static const struct vector_accel_config vector_accel_config_##n = {                       \
        .min_factor = DT_INST_PROP(n, min_factor),                                            \
        .max_factor = DT_INST_PROP(n, max_factor),                                            \
        .unity_speed = DT_INST_PROP(n, unity_speed),                                          \
        .max_speed = DT_INST_PROP(n, max_speed),                                              \
    };                                                                                         \
    static struct vector_accel_data vector_accel_data_##n;                                    \
    DEVICE_DT_INST_DEFINE(n, vector_accel_init, NULL, &vector_accel_data_##n,                  \
                          &vector_accel_config_##n, POST_KERNEL,                               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &vector_accel_api);

DT_INST_FOREACH_STATUS_OKAY(VECTOR_ACCEL_INST)
