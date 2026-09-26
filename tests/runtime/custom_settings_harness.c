/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM "amgskobo__accel"

enum zmk_custom_setting_value_type {
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 = 1,
    ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,
};
struct zmk_custom_setting_value {
    enum zmk_custom_setting_value_type type;
    union {
        int32_t int32_value;
        bool bool_value;
    };
};
struct zmk_custom_setting {
    const char *custom_subsystem_id;
    const char *key;
    int read_result;
    struct zmk_custom_setting_value value;
};
typedef struct { int unused; } zmk_custom_CallRequest;
typedef struct { int unused; } pb_callback_t;

#define ARG_UNUSED(x) ((void)(x))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static int errors;
#define LOG_ERR(fmt, ...) ((void)errors++, (void)sizeof(printf(fmt, ##__VA_ARGS__)))

static struct zmk_custom_setting *settings;
static size_t settings_len;
#define ZMK_CUSTOM_SETTING_FOREACH(_var)                                                           \
    for (struct zmk_custom_setting *_var = settings; _var < settings + settings_len; _var++)

static int zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                                   struct zmk_custom_setting_value *value) {
    *value = setting->value;
    return setting->read_result;
}

/* The apply step: one devicetree instance, its running curve and four values. */
#include <zmk-input-vector-acceleration/vector_accel_runtime.h>
#define ZMK_EV_EVENT_BUBBLE 0
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DEVICE_DT_INST_GET(n) (&instance)
#define LOG_WRN(fmt, ...) ((void)warnings++, (void)sizeof(printf(fmt, ##__VA_ARGS__)))
typedef struct { int kind; } zmk_event_t;
static int warnings;
static struct device instance;
#define INT_SETTING(name, v)                                                                       \
    static struct zmk_custom_setting name = {                                                      \
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, .int32_value = (v)}}
INT_SETTING(vector_accel_cs_min_factor_0, 400);
INT_SETTING(vector_accel_cs_max_factor_0, 3000);
INT_SETTING(vector_accel_cs_unity_speed_0, 100);
INT_SETTING(vector_accel_cs_max_speed_0, 900);
static struct vector_accel_config running = {500, 2000, 50, 500};
static int get_result;
static int applies;

int vector_accel_get_config(const struct device *dev, struct vector_accel_config *out) {
    assert(dev == &instance);
    *out = running;
    return get_result;
}

int vector_accel_set_config(const struct device *dev, const struct vector_accel_config *config) {
    assert(dev == &instance);
    applies++;
    running = *config;
    return 0;
}

/* DRIVER_FUNCTIONS */

static struct zmk_custom_setting int32_setting(int32_t value) {
    return (struct zmk_custom_setting){
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, .int32_value = value}};
}

static struct zmk_custom_setting bool_setting(bool value) {
    return (struct zmk_custom_setting){
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, .bool_value = value}};
}

static void test_readers(void) {
    uint32_t number = 7;
    uint16_t narrow = 7;

    /* Non-negative int32 only, whatever the stored record holds. */
    struct zmk_custom_setting setting = int32_setting(1000000);
    assert(read_int32(&setting, &number) && number == 1000000);
    setting = int32_setting(0);
    assert(read_int32(&setting, &number) && number == 0);
    number = 7;
    setting = int32_setting(-1);
    assert(!read_int32(&setting, &number) && number == 7);
    setting = int32_setting(5);
    setting.read_result = -2;
    assert(!read_int32(&setting, &number) && number == 7);
    setting = bool_setting(true);
    assert(!read_int32(&setting, &number) && number == 7);

    /* The factors are 16-bit: a wider value is refused, not truncated. */
    setting = int32_setting(UINT16_MAX);
    assert(read_uint16(&setting, &narrow) && narrow == UINT16_MAX);
    narrow = 7;
    setting = int32_setting(UINT16_MAX + 1);
    assert(!read_uint16(&setting, &narrow) && narrow == 7);
    setting = int32_setting(-3);
    assert(!read_uint16(&setting, &narrow) && narrow == 7);
}

static void test_unique_keys(void) {
    struct zmk_custom_setting list[] = {
        {.custom_subsystem_id = "other", .key = "accel.width"},
        {.custom_subsystem_id = ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM, .key = "accel.width"},
        {.custom_subsystem_id = "other", .key = "accel.layer"},
        {.custom_subsystem_id = ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM, .key = "accel.layer"},
        /* Another subsystem reusing a key after ours is not a duplicate. */
        {.custom_subsystem_id = "other", .key = "accel.width"},
        {.custom_subsystem_id = ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM, .key = "accel.width"},
    };
    settings = list;

    settings_len = ARRAY_SIZE(list);
    assert(vector_accel_check_unique_keys() == 0);
    assert(errors == 1);

    errors = 0;
    settings_len = ARRAY_SIZE(list) - 1;
    assert(vector_accel_check_unique_keys() == 0);
    assert(errors == 0);
}

/* The curve is applied whole, only when it is readable and valid. */
static void test_apply(void) {
    const zmk_event_t changed = {1};
    assert(vector_accel_settings_event_cb(&changed) == ZMK_EV_EVENT_BUBBLE);
    assert(applies == 1 && running.min_factor == 400 && running.max_factor == 3000);
    assert(running.unity_speed == 100 && running.max_speed == 900);

    /* The running curve unknown, or any value unreadable: nothing. */
    get_result = -19;
    vector_accel_apply_settings();
    get_result = 0;
    struct zmk_custom_setting *values[] = {&vector_accel_cs_min_factor_0,
                                           &vector_accel_cs_max_factor_0,
                                           &vector_accel_cs_unity_speed_0,
                                           &vector_accel_cs_max_speed_0};
    for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
        values[i]->read_result = -2;
        vector_accel_apply_settings();
        values[i]->read_result = 0;
    }
    assert(applies == 1 && warnings == 0);

    /* Readable but not a curve - top speed below unity - is reported. */
    vector_accel_cs_max_speed_0.value.int32_value = 50;
    vector_accel_apply_settings();
    assert(applies == 1 && warnings == 1 && running.max_speed == 900);
}

int main(void) {
    assert(!vector_accel_namespace_handler(NULL, NULL));
    test_readers();
    test_unique_keys();
    test_apply();
    puts("vector-acceleration custom settings helpers: PASS");
    return 0;
}
