/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <drivers/input_processor.h>

#include <zmk-input-vector-acceleration/vector_accel_runtime.h>

static const struct device *const accel = DEVICE_DT_GET(DT_NODELABEL(vector_accel_runtime_test));
static const struct device *const other = DEVICE_DT_GET(DT_CHOSEN(zmk_kscan));

static const struct vector_accel_config defaults = {
    .min_factor = 500,
    .max_factor = 3200,
    .unity_speed = 1200,
    .max_speed = 6000,
};

static const struct vector_accel_config curve_a = {
    .min_factor = 250,
    .max_factor = 4000,
    .unity_speed = 900,
    .max_speed = 7000,
};

static const struct vector_accel_config curve_b = {
    .min_factor = 750,
    .max_factor = 12000,
    .unity_speed = 3000,
    .max_speed = 20000,
};

static atomic_t writer_running;
static K_THREAD_STACK_DEFINE(writer_stack, 2048);
static struct k_thread writer_thread;

static bool same_config(const struct vector_accel_config *left,
                        const struct vector_accel_config *right) {
    return left->min_factor == right->min_factor && left->max_factor == right->max_factor &&
           left->unity_speed == right->unity_speed && left->max_speed == right->max_speed;
}

static void writer(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (int i = 0; i < 5000; i++) {
        const struct vector_accel_config *config = (i & 1) == 0 ? &curve_a : &curve_b;

        __ASSERT_NO_MSG(vector_accel_set_config(accel, config) == 0);
    }

    atomic_clear(&writer_running);
}

static struct input_event rel_x(int32_t value, bool sync) {
    return (struct input_event){
        .type = INPUT_EV_REL,
        .code = INPUT_REL_X,
        .value = value,
        .sync = sync,
    };
}

static void process(struct input_event *event, int16_t *remainder) {
    struct zmk_input_processor_state state = {
        .input_device_index = 0,
        .remainder = remainder,
    };

    __ASSERT_NO_MSG(zmk_input_processor_handle_event(accel, event, 0, 0, &state) ==
                    ZMK_INPUT_PROC_CONTINUE);
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct vector_accel_config config;
    struct input_event event;
    int16_t remainder = 0;

    __ASSERT_NO_MSG(device_is_ready(accel));
    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT_NO_MSG(same_config(&config, &defaults));
    __ASSERT_NO_MSG(vector_accel_get_config(NULL, &config) == -EINVAL);
    __ASSERT_NO_MSG(vector_accel_get_config(accel, NULL) == -EINVAL);
    __ASSERT_NO_MSG(vector_accel_get_config(other, &config) == -ENODEV);

    config = defaults;
    config.max_speed = config.unity_speed;
    __ASSERT_NO_MSG(vector_accel_set_config(accel, &config) == -EINVAL);
    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT_NO_MSG(same_config(&config, &defaults));

    event = rel_x(100, true);
    process(&event, &remainder);
    __ASSERT_NO_MSG(event.value == 100);
    k_sleep(K_MSEC(10));

    event = rel_x(100, true);
    process(&event, &remainder);
    __ASSERT_NO_MSG(event.value == 100);
    k_sleep(K_MSEC(10));

    event = rel_x(100, true);
    process(&event, &remainder);
    __ASSERT_NO_MSG(event.value == 320);

    event = (struct input_event){
        .type = INPUT_EV_KEY,
        .code = INPUT_KEY_0,
        .value = 7,
        .sync = true,
    };
    process(&event, &remainder);
    __ASSERT_NO_MSG(event.value == 7);

    atomic_set(&writer_running, 1);
    k_tid_t writer_id =
        k_thread_create(&writer_thread, writer_stack, K_THREAD_STACK_SIZEOF(writer_stack), writer,
                        NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);

    do {
        __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
        __ASSERT_NO_MSG(same_config(&config, &defaults) || same_config(&config, &curve_a) ||
                        same_config(&config, &curve_b));
        k_yield();
    } while (atomic_get(&writer_running));

    __ASSERT_NO_MSG(k_thread_join(writer_id, K_SECONDS(5)) == 0);
    __ASSERT_NO_MSG(vector_accel_set_config(accel, &defaults) == 0);

    printk("vector acceleration runtime tests: PASS\n");
    exit(0);
}

K_THREAD_DEFINE(vector_accel_runtime_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
