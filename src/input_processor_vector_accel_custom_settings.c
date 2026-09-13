/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes the curve through zmk-feature-custom-settings.
 *
 * Registering here rather than inventing a private RPC is what makes the
 * values show up in a Studio client with no page of their own: custom-settings
 * is a cross-module registry, and a client that renders it renders these too,
 * with the declared type and range driving the widget. The four values are
 * plain scalars with bounds, which is exactly what that registry is for -- a
 * module only needs its own protocol when it has something to show that a
 * generic settings list cannot draw.
 *
 * custom-settings owns persistence while this file is compiled in; the
 * processor's own settings writes are compiled out to keep one owner.
 */

#define DT_DRV_COMPAT zmk_input_processor_vector_acceleration

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>
#include <zmk/studio/custom.h>

#include <zmk-input-vector-acceleration/custom_settings.h>
#include <zmk-input-vector-acceleration/vector_accel_core.h>
#include <zmk-input-vector-acceleration/vector_accel_runtime.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static bool vector_accel_namespace_handler(const zmk_custom_CallRequest *request,
                                           pb_callback_t *encode_response);

/*
 * A setting belongs to a custom subsystem, and custom-settings resolves that
 * identifier to an index before it can put the setting on the wire: without a
 * registered subsystem of the same name, every one of these settings is
 * dropped with -ENOENT and never reaches a client, however correctly it was
 * defined. So the subsystem is registered here purely to own the namespace.
 *
 * It answers no calls of its own -- the values are read and written through
 * custom-settings' own RPC -- so the handler declines every request and the
 * module needs no protocol, no nanopb, and no page of its own. The advertised
 * UI is the custom settings editor, which is where these values are edited.
 */
static struct zmk_rpc_custom_subsystem_meta vector_accel_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://github.com/amgskobo/zmk-input-vector-acceleration"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

/*
 * Through a wrapper so the token expands before it is stringified.
 *
 * ZMK_RPC_CUSTOM_SUBSYSTEM registers `#_identifier`, and `#` suppresses
 * expansion of its own argument, so passing the macro straight in would
 * register the literal text "ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM_TOKEN". One
 * more layer of call expands it first.
 */
#define REGISTER_SUBSYSTEM(identifier, meta, handler)                                              \
    ZMK_RPC_CUSTOM_SUBSYSTEM(identifier, meta, handler)

REGISTER_SUBSYSTEM(ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM_TOKEN, &vector_accel_meta,
                   vector_accel_namespace_handler);

static bool vector_accel_namespace_handler(const zmk_custom_CallRequest *request,
                                           pb_callback_t *encode_response) {
    ARG_UNUSED(request);
    ARG_UNUSED(encode_response);

    return false;
}

#define VECTOR_ACCEL_SETTING(n, field, key, lo, hi)                                                \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        vector_accel_cs_##field##_##n, ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM,                    \
        ZMK_INPUT_VECTOR_ACCELERATION_SETTING_KEY(n, key), ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,    \
        ZMK_CUSTOM_SETTING_VALUE_INT32(DT_INST_PROP(n, field)),                                    \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(lo, hi));

/*
 * The ranges repeat the devicetree BUILD_ASSERTs so a client can grey out an
 * impossible value before sending it. They cannot express "max-speed must
 * exceed unity-speed", so vector_accel_config_valid() still has the last word
 * when the four are applied together.
 */
#define VECTOR_ACCEL_SETTINGS(n)                                                                   \
    ZMK_INPUT_VECTOR_ACCELERATION_ASSERT_NAME_FITS(n, "unity_speed")                               \
    VECTOR_ACCEL_SETTING(n, min_factor, "min_factor", VECTOR_ACCEL_MIN_FACTOR_FLOOR,               \
                         VECTOR_ACCEL_SCALE)                                                       \
    VECTOR_ACCEL_SETTING(n, max_factor, "max_factor", VECTOR_ACCEL_SCALE,                          \
                         VECTOR_ACCEL_MAX_FACTOR_CEILING)                                          \
    VECTOR_ACCEL_SETTING(n, unity_speed, "unity_speed", 1, 1000000)                                \
    VECTOR_ACCEL_SETTING(n, max_speed, "max_speed", 2, 1000000)

DT_INST_FOREACH_STATUS_OKAY(VECTOR_ACCEL_SETTINGS)

static bool read_int32(const struct zmk_custom_setting *setting, uint32_t *out) {
    struct zmk_custom_setting_value value;

    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 || value.int32_value < 0) {
        return false;
    }

    *out = (uint32_t)value.int32_value;

    return true;
}

static bool read_uint16(const struct zmk_custom_setting *setting, uint16_t *out) {
    uint32_t value;

    if (!read_int32(setting, &value) || value > UINT16_MAX) {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

/*
 * Applied as a set: a client that moves one value at a time would otherwise
 * pass through combinations the curve rejects, and a half-applied curve is
 * worse than a stale one.
 */
#define VECTOR_ACCEL_APPLY(n)                                                                      \
    {                                                                                              \
        struct vector_accel_config config;                                                         \
        const struct device *dev = DEVICE_DT_INST_GET(n);                                          \
                                                                                                   \
        if (vector_accel_get_config(dev, &config) == 0 &&                                          \
            read_uint16(&vector_accel_cs_min_factor_##n, &config.min_factor) &&                    \
            read_uint16(&vector_accel_cs_max_factor_##n, &config.max_factor) &&                    \
            read_int32(&vector_accel_cs_unity_speed_##n, &config.unity_speed) &&                   \
            read_int32(&vector_accel_cs_max_speed_##n, &config.max_speed)) {                       \
            if (vector_accel_config_valid(&config)) {                                              \
                (void)vector_accel_set_config(dev, &config);                                       \
            } else {                                                                               \
                LOG_WRN("vector-accel: settings for instance %d do not form a valid curve", n);    \
            }                                                                                      \
        }                                                                                          \
    }

static void vector_accel_apply_settings(void) { DT_INST_FOREACH_STATUS_OKAY(VECTOR_ACCEL_APPLY) }

static int vector_accel_settings_event_cb(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    /*
     * Both subscribed events mean the same thing here -- some stored value may
     * now differ from what the processor is running -- and re-reading every
     * instance is cheaper than working out which one moved.
     */
    vector_accel_apply_settings();

    return ZMK_EV_EVENT_BUBBLE;
}

/*
 * Applied on two signals, and deliberately not from a SYS_INIT.
 *
 * zmk_custom_settings_initialized fires from the settings-subtree commit that
 * ends the boot settings_load pass, which is the only point at which a stored
 * value is both present and readable. A SYS_INIT is too early: it runs before
 * settings_load(), so it would read the devicetree default and leave the
 * processor on it for the rest of the session -- the value would persist and
 * show correctly in a client while having no effect on the hardware.
 *
 * The load path stores values without raising zmk_custom_setting_changed, so
 * that event alone would never deliver a stored value either. Together the two
 * cover boot and every later edit.
 *
 * In a build without CONFIG_SETTINGS nothing is stored and the event never
 * fires, which is correct: the driver already starts from its devicetree
 * values.
 */
ZMK_LISTENER(vector_accel_custom_settings, vector_accel_settings_event_cb);
ZMK_SUBSCRIPTION(vector_accel_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(vector_accel_custom_settings, zmk_custom_settings_initialized);

/*
 * Two nodes can still produce one key, and nothing downstream would say so.
 *
 * A key is the owning node's DT_NODE_FULL_NAME, which is the node's own name
 * and not its path, so devicetree keeps it unique only among its siblings. A
 * board that puts a processor under /input_processors and a module that puts
 * one at the root can pick the same name and neither Zephyr nor the settings
 * registry objects: zmk_custom_setting_find() returns the first match, so a
 * client's write always lands on whichever linked first while the second
 * silently keeps its devicetree values and appears in the list as though it
 * were being edited. A stored value restores into only one of them too.
 *
 * String equality across instances is not something the preprocessor can
 * evaluate, so the check runs once at startup and names the duplicated key. It
 * walks descriptors, not values, so it needs nothing from settings_load().
 */
static int vector_accel_check_unique_keys(void) {
    ZMK_CUSTOM_SETTING_FOREACH(setting) {
        if (strcmp(setting->custom_subsystem_id, ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM) != 0) {
            continue;
        }

        ZMK_CUSTOM_SETTING_FOREACH(other) {
            if (other == setting) {
                /* Only report a pair once: stop at the first of the two. */
                break;
            }

            if (strcmp(other->custom_subsystem_id, ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM) == 0 &&
                strcmp(other->key, setting->key) == 0) {
                LOG_ERR("Duplicate setting key \"%s\": two devicetree nodes share a "
                        "name, so only one of them is editable",
                        setting->key);
            }
        }
    }

    return 0;
}

SYS_INIT(vector_accel_check_unique_keys, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
