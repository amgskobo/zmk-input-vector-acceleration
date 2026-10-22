/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <cormoran/zmk/custom_settings.h>

#include <zmk-input-vector-acceleration/custom_settings.h>
#include <zmk-input-vector-acceleration/vector_accel_runtime.h>

static const struct device *const accel = DEVICE_DT_GET(DT_NODELABEL(vector_accel_test));

static const struct zmk_custom_setting *find(const char *key) {
    return zmk_custom_setting_find(ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM, key);
}

static int write_int(const char *key, int32_t value) {
    const struct zmk_custom_setting *setting = find(key);

    __ASSERT_NO_MSG(setting != NULL);
    return zmk_custom_setting_write(setting, &ZMK_CUSTOM_SETTING_VALUE_INT32(value),
                                    ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
}

static bool same_config(const struct vector_accel_config *config, uint16_t min_factor,
                        uint16_t max_factor, uint32_t unity_speed, uint32_t max_speed) {
    return config->min_factor == min_factor && config->max_factor == max_factor &&
           config->unity_speed == unity_speed && config->max_speed == max_speed;
}

K_SEM_DEFINE(snapshot_ready, 0, 1);
K_SEM_DEFINE(snapshot_resume, 0, 1);
K_SEM_DEFINE(writers_done, 0, 2);
K_THREAD_STACK_DEFINE(writer_stack_a, 4096);
K_THREAD_STACK_DEFINE(writer_stack_b, 4096);
static struct k_thread writer_a, writer_b;
static const struct zmk_custom_setting *race_max;
static atomic_t pause_read;

int __real_zmk_custom_setting_read(const struct zmk_custom_setting *,
                                  struct zmk_custom_setting_value *);
int __wrap_zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                                  struct zmk_custom_setting_value *value) {
    int rc = __real_zmk_custom_setting_read(setting, value);
    if (rc == 0 && setting == race_max && k_current_get() == &writer_a &&
        atomic_cas(&pause_read, 1, 0)) {
        k_sem_give(&snapshot_ready);
        __ASSERT_NO_MSG(k_sem_take(&snapshot_resume, K_SECONDS(1)) == 0);
    }
    return rc;
}

static void concurrent_writer(void *which, void *b, void *c) {
    ARG_UNUSED(b); ARG_UNUSED(c);
    __ASSERT_NO_MSG(write_int(which == NULL ? "vector_accel_test.min_factor" :
                             "vector_accel_test.max_speed",
                             which == NULL ? 700 : 10000) == 0);
    k_sem_give(&writers_done);
}

static void test_concurrent_settings(void) {
    race_max = find("vector_accel_test.max_speed");
    __ASSERT_NO_MSG(race_max != NULL);
    atomic_set(&pause_read, 1);
    k_thread_create(&writer_a, writer_stack_a, K_THREAD_STACK_SIZEOF(writer_stack_a),
                    concurrent_writer, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
    __ASSERT_NO_MSG(k_sem_take(&snapshot_ready, K_SECONDS(1)) == 0);
    k_thread_create(&writer_b, writer_stack_b, K_THREAD_STACK_SIZEOF(writer_stack_b),
                    concurrent_writer, (void *)1, NULL, NULL, 1, 0, K_NO_WAIT);
    /* B outranks this test thread and commits before it finishes or blocks
     * waiting for the adapter. The registry lock is outside the pause. */
    struct zmk_custom_setting_value stored;
    __ASSERT_NO_MSG(zmk_custom_setting_read(race_max, &stored) == 0 &&
                    stored.int32_value == 10000);
    k_sem_give(&snapshot_resume);
    __ASSERT_NO_MSG(k_sem_take(&writers_done, K_SECONDS(1)) == 0);
    __ASSERT_NO_MSG(k_sem_take(&writers_done, K_SECONDS(1)) == 0);
    struct vector_accel_config config;
    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT(same_config(&config, 700, 20000, 7000, 10000),
             "Concurrent registry edits must not restore an older curve");
    printk("vector acceleration concurrent settings: PASS\n");
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    static const char *const keys[] = {
        "vector_accel_test.min_factor",
        "vector_accel_test.max_factor",
        "vector_accel_test.unity_speed",
        "vector_accel_test.max_speed",
    };
    struct vector_accel_config config;
    size_t published = 0;

    ZMK_CUSTOM_SETTING_FOREACH(setting) {
        if (strcmp(setting->custom_subsystem_id, ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM) == 0) {
            published++;
        }
    }
    __ASSERT_NO_MSG(published == ARRAY_SIZE(keys));

    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        const struct zmk_custom_setting *setting = find(keys[i]);

        __ASSERT_NO_MSG(setting != NULL);
        __ASSERT_NO_MSG(setting->value_type == ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32);
        __ASSERT_NO_MSG(setting->confidentiality == ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC);
        __ASSERT_NO_MSG(setting->read_permission == ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE);
        __ASSERT_NO_MSG(setting->write_permission == ZMK_CUSTOM_SETTING_PERMISSION_SECURE);
    }

    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT_NO_MSG(same_config(&config, 500, 3200, 1200, 6000));

    __ASSERT_NO_MSG(write_int(keys[0], 600) == 0);
    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT_NO_MSG(same_config(&config, 600, 3200, 1200, 6000));

    __ASSERT_NO_MSG(write_int(keys[2], 7000) == 0);
    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT_NO_MSG(same_config(&config, 600, 3200, 1200, 6000));

    __ASSERT_NO_MSG(write_int(keys[3], 9000) == 0);
    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT_NO_MSG(same_config(&config, 600, 3200, 7000, 9000));

    __ASSERT_NO_MSG(write_int(keys[1], 20000) == 0);
    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT_NO_MSG(same_config(&config, 600, 20000, 7000, 9000));

    __ASSERT_NO_MSG(write_int(keys[0], 99) < 0);
    __ASSERT_NO_MSG(vector_accel_get_config(accel, &config) == 0);
    __ASSERT_NO_MSG(same_config(&config, 600, 20000, 7000, 9000));

    test_concurrent_settings();
    printk("vector acceleration Studio settings tests: PASS\n");
    exit(0);
}

K_THREAD_DEFINE(vector_accel_studio_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
