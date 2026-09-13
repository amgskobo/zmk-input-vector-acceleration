/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <zmk-input-vector-acceleration/vector_accel_runtime.h>

static const struct device *const accel =
    DEVICE_DT_GET(DT_NODELABEL(vector_accel_persistence_test));

static const struct vector_accel_config defaults = {
    .min_factor = 500,
    .max_factor = 3200,
    .unity_speed = 1200,
    .max_speed = 6000,
};

static const struct vector_accel_config stored = {
    .min_factor = 350,
    .max_factor = 4800,
    .unity_speed = 1500,
    .max_speed = 9000,
};

static bool same_config(const struct vector_accel_config *left,
                        const struct vector_accel_config *right) {
    return left->min_factor == right->min_factor && left->max_factor == right->max_factor &&
           left->unity_speed == right->unity_speed && left->max_speed == right->max_speed;
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct vector_accel_config config;

    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);

    if (same_config(&config, &defaults)) {
        __ASSERT_NO_MSG(vector_accel_set_config(accel, &stored) == 0);
        k_sleep(K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE + 100));
        printk("vector acceleration persistence first boot: PASS\n");
    } else {
        __ASSERT_NO_MSG(same_config(&config, &stored));
        printk("vector acceleration persistence second boot: PASS\n");
    }

    exit(0);
}

K_THREAD_DEFINE(vector_accel_persistence_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
