/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_vector_acceleration

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <zmk/workqueue.h>

/*
 * When the custom-settings integration is compiled in it owns persistence, so
 * this file keeps only the in-RAM copy. Two writers for one value is how a
 * stored setting and a live setting drift apart.
 */
#if IS_ENABLED(CONFIG_SETTINGS) && !IS_ENABLED(CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_CUSTOM_SETTINGS)
#define VECTOR_ACCEL_OWNS_PERSISTENCE 1
#include <stdio.h>
#include <zephyr/settings/settings.h>
#endif

#include <zmk-input-vector-acceleration/vector_accel_core.h>
#include <zmk-input-vector-acceleration/vector_accel_runtime.h>

#define VECTOR_ACCEL_LISTENER_COUNT DT_NUM_INST_STATUS_OKAY(zmk_input_listener)
#define VECTOR_ACCEL_STREAM_COUNT MAX(VECTOR_ACCEL_LISTENER_COUNT, 1)

struct vector_accel_data {
    struct vector_accel_stream streams[VECTOR_ACCEL_STREAM_COUNT];
    int16_t fallback_remainders[VECTOR_ACCEL_STREAM_COUNT][VECTOR_ACCEL_AXIS_COUNT];
    struct k_spinlock config_lock;
    /*
     * The curve values live here rather than in the devicetree struct so a
     * client can move them while the keyboard is in use. dev->config keeps
     * the devicetree values, which stay available as the reset target.
     */
    struct vector_accel_config config;
#if IS_ENABLED(VECTOR_ACCEL_OWNS_PERSISTENCE)
    /*
     * Flash writes are pushed onto a work queue rather than done in the
     * caller. A runtime client may set values interactively -- often several in a
     * row while it settles on a feel -- and a synchronous save would both
     * stall the RPC response past the client's timeout and burn a flash write
     * on every intermediate value. The curve itself changes immediately
     * regardless: the hot path reads data->config, not the stored copy.
     */
    struct k_work_delayable save_work;
#endif
};

static struct vector_accel_config vector_accel_config_snapshot(struct vector_accel_data *data) {
    k_spinlock_key_t key = k_spin_lock(&data->config_lock);
    struct vector_accel_config config = data->config;

    k_spin_unlock(&data->config_lock, key);
    return config;
}

static void vector_accel_config_replace(struct vector_accel_data *data,
                                        const struct vector_accel_config *config) {
    k_spinlock_key_t key = k_spin_lock(&data->config_lock);

    data->config = *config;
    k_spin_unlock(&data->config_lock, key);
}

static struct vector_accel_stream *stream_for_event(
    struct vector_accel_data *data, const struct zmk_input_processor_state *state,
    size_t *stream_index) {
    size_t index;

    if (state == NULL) {
        index = 0U;
    } else if (state->input_device_index >= VECTOR_ACCEL_STREAM_COUNT) {
        return NULL;
    } else {
        index = state->input_device_index;
    }

    *stream_index = index;
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

    struct vector_accel_data *data = dev->data;
    size_t stream_index;
    struct vector_accel_stream *stream = stream_for_event(data, state, &stream_index);

    if (stream == NULL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
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
        struct vector_accel_config config = vector_accel_config_snapshot(data);

        vector_accel_stream_finish_frame(stream, &config, now_ms);
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

#if IS_ENABLED(VECTOR_ACCEL_OWNS_PERSISTENCE)
static void vector_accel_save_work_handler(struct k_work *work);
#endif

static int vector_accel_init(const struct device *dev) {
    struct vector_accel_data *data = dev->data;

    for (size_t i = 0U; i < VECTOR_ACCEL_STREAM_COUNT; i++) {
        vector_accel_stream_init(&data->streams[i]);
    }

    /*
     * Start from the devicetree values. A persisted override, if there is
     * one, is applied later from an APPLICATION-priority SYS_INIT, once the
     * settings subsystem is up.
     */
    data->config = *(const struct vector_accel_config *)dev->config;

#if IS_ENABLED(VECTOR_ACCEL_OWNS_PERSISTENCE)
    k_work_init_delayable(&data->save_work, vector_accel_save_work_handler);
#endif

    return 0;
}

static const struct zmk_input_processor_driver_api vector_accel_api = {
    .handle_event = vector_accel_handle_event,
};

#define VECTOR_ACCEL_INST(n)                                                                       \
    BUILD_ASSERT(DT_INST_PROP(n, min_factor) >= 100 &&                                             \
                     DT_INST_PROP(n, min_factor) <= VECTOR_ACCEL_SCALE,                            \
                 "min-factor must be between 100 and 1000");                                       \
    BUILD_ASSERT(DT_INST_PROP(n, max_factor) >= VECTOR_ACCEL_SCALE &&                              \
                     DT_INST_PROP(n, max_factor) <= 20000,                                         \
                 "max-factor must be between 1000 and 20000");                                     \
    BUILD_ASSERT(DT_INST_PROP(n, unity_speed) > 0, "unity-speed must be positive");                \
    BUILD_ASSERT(DT_INST_PROP(n, max_speed) > DT_INST_PROP(n, unity_speed),                        \
                 "max-speed must be greater than unity-speed");                                    \
    static const struct vector_accel_config vector_accel_config_##n = {                            \
        .min_factor = DT_INST_PROP(n, min_factor),                                                 \
        .max_factor = DT_INST_PROP(n, max_factor),                                                 \
        .unity_speed = DT_INST_PROP(n, unity_speed),                                               \
        .max_speed = DT_INST_PROP(n, max_speed),                                                   \
    };                                                                                             \
    static struct vector_accel_data vector_accel_data_##n;                                         \
    DEVICE_DT_INST_DEFINE(n, vector_accel_init, NULL, &vector_accel_data_##n,                      \
                          &vector_accel_config_##n, POST_KERNEL,                                   \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &vector_accel_api);

DT_INST_FOREACH_STATUS_OKAY(VECTOR_ACCEL_INST)

#define VECTOR_ACCEL_DEV_REF(n) DEVICE_DT_INST_GET(n),

static const struct device *const vector_accel_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(VECTOR_ACCEL_DEV_REF)};

static int vector_accel_device_index(const struct device *dev) {
    for (size_t i = 0U; i < ARRAY_SIZE(vector_accel_devices); i++) {
        if (vector_accel_devices[i] == dev) {
            return (int)i;
        }
    }

    return -ENODEV;
}

#if IS_ENABLED(VECTOR_ACCEL_OWNS_PERSISTENCE)

#define VECTOR_ACCEL_SETTINGS_ROOT "vaccel"

static int vector_accel_settings_store(int index, const struct vector_accel_config *config) {
    char key[sizeof(VECTOR_ACCEL_SETTINGS_ROOT) + 12];

    (void)snprintf(key, sizeof(key), VECTOR_ACCEL_SETTINGS_ROOT "/%d", index);

    return settings_save_one(key, config, sizeof(*config));
}

static int vector_accel_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                     void *cb_arg) {
    struct vector_accel_config stored;
    const char *next;
    char *end;
    long index;
    int rc;

    if (settings_name_next(name, &next) == 0 || next != NULL) {
        return -ENOENT;
    }

    errno = 0;
    index = strtol(name, &end, 10);
    if (errno != 0 || end == name || *end != '\0' || index < 0 ||
        (size_t)index >= ARRAY_SIZE(vector_accel_devices)) {
        return -ENOENT;
    }

    /*
     * A stored value that no longer matches the struct, or that falls outside
     * the bounds, is dropped rather than applied: the devicetree defaults are
     * already in place, so ignoring it leaves a working processor.
     */
    if (len != sizeof(stored)) {
        return 0;
    }

    rc = read_cb(cb_arg, &stored, sizeof(stored));
    if (rc != (int)sizeof(stored)) {
        return rc < 0 ? rc : 0;
    }

    if (!vector_accel_config_valid(&stored)) {
        return 0;
    }

    struct vector_accel_data *data = vector_accel_devices[index]->data;

    vector_accel_config_replace(data, &stored);

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(vector_accel, VECTOR_ACCEL_SETTINGS_ROOT, NULL,
                               vector_accel_settings_set, NULL, NULL);

/*
 * The handler runs long after the request that scheduled it, so it works out
 * which instance it belongs to from the data pointer rather than carrying an
 * index around.
 */
static void vector_accel_save_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct vector_accel_data *data = CONTAINER_OF(dwork, struct vector_accel_data, save_work);
    struct vector_accel_config config = vector_accel_config_snapshot(data);

    for (size_t i = 0U; i < ARRAY_SIZE(vector_accel_devices); i++) {
        if (vector_accel_devices[i]->data != data) {
            continue;
        }

        (void)vector_accel_settings_store((int)i, &config);
        return;
    }
}

static int vector_accel_schedule_save(struct vector_accel_data *data) {
    return MIN(0, k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &data->save_work,
                                              K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE)));
}

static int vector_accel_settings_load(void) {
    int rc = settings_subsys_init();

    if (rc != 0) {
        return rc;
    }

    return settings_load_subtree(VECTOR_ACCEL_SETTINGS_ROOT);
}

/*
 * Runs after every processor's POST_KERNEL init, so the devicetree values are
 * already in RAM and a stored override simply replaces them.
 */
SYS_INIT(vector_accel_settings_load, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#else

static int vector_accel_schedule_save(struct vector_accel_data *data) {
    ARG_UNUSED(data);

    return 0;
}

#endif /* IS_ENABLED(VECTOR_ACCEL_OWNS_PERSISTENCE) */

int vector_accel_get_config(const struct device *dev, struct vector_accel_config *out) {
    struct vector_accel_data *data;
    int index;

    if (dev == NULL || out == NULL) {
        return -EINVAL;
    }

    index = vector_accel_device_index(dev);
    if (index < 0) {
        return index;
    }

    data = dev->data;
    *out = vector_accel_config_snapshot(data);

    return 0;
}

int vector_accel_set_config(const struct device *dev, const struct vector_accel_config *config) {
    struct vector_accel_data *data;
    int index;

    if (dev == NULL || !vector_accel_config_valid(config)) {
        return -EINVAL;
    }

    index = vector_accel_device_index(dev);
    if (index < 0) {
        return index;
    }

    data = dev->data;
    vector_accel_config_replace(data, config);

    return vector_accel_schedule_save(data);
}
