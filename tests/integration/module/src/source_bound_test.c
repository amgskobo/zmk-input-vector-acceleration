/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <drivers/input_processor.h>
#include <zmk-input-vector-acceleration/vector_accel_runtime.h>

#define SOURCE_A CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_TEST_SOURCE_BASE
#if SOURCE_A == 0
#define SOURCE_B 1
#else
#define SOURCE_B 4
#endif
BUILD_ASSERT(DT_SAME_NODE(DT_INST(SOURCE_A, zmk_input_listener), DT_NODELABEL(sb_listener_a)),
             "source-bound fixture listener A index changed");
BUILD_ASSERT(DT_SAME_NODE(DT_INST(SOURCE_B, zmk_input_listener), DT_NODELABEL(sb_listener_b)),
             "source-bound fixture listener B index changed");

static const struct device *const nodes[] = {
    DEVICE_DT_GET(DT_NODELABEL(sb_accel_a)), DEVICE_DT_GET(DT_NODELABEL(sb_accel_b)),
};

static int32_t report(const struct device *dev, uint8_t source, int32_t raw, int16_t *remainder) {
    struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_X, .value = raw, .sync = true};
    struct zmk_input_processor_state state = {.input_device_index = source, .remainder = remainder};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(dev, &event, 0, 0, &state) == 0);
    return event.value;
}

static void run_tests(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (size_t node = 0; node < ARRAY_SIZE(nodes); node++) {
        __ASSERT_NO_MSG(device_is_ready(nodes[node]));
        for (uint8_t source = SOURCE_A; source <= SOURCE_B; source++) {
            __ASSERT_NO_MSG(report(nodes[node], source, 10000, NULL) == 10000);
            __ASSERT_NO_MSG(report(nodes[node], source, 10000, NULL) == 10000);
            __ASSERT_NO_MSG(report(nodes[node], source, 10000, NULL) == 32000);
        }
    }
    struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_X, .value = 10000,
                                .sync = true};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(nodes[0], &event, 0, 0, NULL) == 0);
    __ASSERT_NO_MSG(event.value == (SOURCE_A == 0 ? 32000 : 10000));

    struct vector_accel_config config;
    __ASSERT_NO_MSG(vector_accel_get_config(nodes[0], &config) == 0);
    config.max_factor = 6400;
    __ASSERT_NO_MSG(vector_accel_set_config(nodes[0], &config) == 0);
    __ASSERT_NO_MSG(report(nodes[0], SOURCE_B, 10000, NULL) == 32000);
    __ASSERT_NO_MSG(report(nodes[0], SOURCE_B, 10000, NULL) == 64000);
    __ASSERT_NO_MSG(report(nodes[1], SOURCE_B, 10000, NULL) == 32000);

#if CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_MAX_SOURCES == 256
    config.min_factor = 500;
    config.max_factor = 1000;
    config.unity_speed = 1000000000;
    config.max_speed = 2000000000;
    __ASSERT_NO_MSG(vector_accel_set_config(nodes[0], &config) == 0);
    for (unsigned int source = 0; source <= UINT8_MAX; source++) {
        (void)report(nodes[0], (uint8_t)source, 2, NULL);
        (void)report(nodes[0], (uint8_t)source, 2, NULL);
        __ASSERT_NO_MSG(report(nodes[0], (uint8_t)source, 2, NULL) == 1);
    }
    printk("vector full prefix: 256 active source IDs PASS\n");
#else
    int16_t remainder = 17;
    __ASSERT_NO_MSG(report(nodes[0], UINT8_MAX, 123, &remainder) == 123);
    __ASSERT_NO_MSG(remainder == 17);
#if CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_MAX_SOURCES == 2
    __ASSERT_NO_MSG(report(nodes[0], 2, 123, &remainder) == 123);
    __ASSERT_NO_MSG(remainder == 17);
#endif
#endif
    printk("vector source-bound runtime: base=%d max=%d two nodes, two sources, NULL, curve PASS\n",
           SOURCE_A, CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_MAX_SOURCES);
    exit(0);
}
K_THREAD_DEFINE(vector_source_bound_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
