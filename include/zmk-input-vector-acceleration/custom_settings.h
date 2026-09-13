/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The custom-settings namespace this module publishes under.
 *
 * A setting is dropped with -ENOENT unless its subsystem is registered, and a
 * client groups the list it renders by subsystem, so this is also the heading
 * a person reads. It is named for the module rather than for anything about
 * the processor, because the module is what the heading collects.
 *
 * The stored settings name is "custom_settings/<subsystem>/<key>" against
 * Zephyr's 64-byte SETTINGS_MAX_NAME_LEN, so every character here is a
 * character taken from every node name in every board that uses this module.
 * The build-time checks below reserve that space instead of allowing a setting
 * to apply for one boot and then silently fail to persist.
 */

#pragma once

#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

/*
 * The identifier is spelled once, as a token.
 *
 * custom-settings takes the subsystem as a string in every setting it
 * registers, while ZMK's ZMK_RPC_CUSTOM_SUBSYSTEM takes it as a token and
 * stringifies it to get the registered id. Spelling it twice is a silent
 * failure if the two ever disagree: the settings define fine, the subsystem
 * registers fine under the other name, and every setting is then dropped with
 * -ENOENT because no subsystem of its name is registered. So the token is the
 * definition and the string is derived from it, and the registration passes
 * the token through a wrapper so that it expands before being stringified.
 */
#define ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM_TOKEN amgskobo__accel
#define ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM                                                    \
    STRINGIFY(ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM_TOKEN)

/*
 * A setting key is the owning node's devicetree name, then the field:
 *
 *     pointer_accel.min_factor
 *     stick_accel.unity_speed
 *
 * The field is the devicetree property with its hyphens as underscores, so a
 * board author reads the same name in both places.
 *
 * The node name is the one identifier both halves of the problem already hold.
 * A view drawing the chain walks devicetree for the processors in each
 * listener and gets a `const struct device *` per stage, whose ->name is
 * DEVICE_DT_NAME(), which is DT_NODE_FULL_NAME() -- the same string this
 * builds the key from. So a curve's settings are the ones whose key starts
 * with its device name, and nothing has to be registered, agreed between
 * modules, or typed into devicetree by a board author for that to hold.
 *
 * It also removes the commonest way to collide, since a name is no longer
 * hand-written -- though not every way, which is what the startup check in the
 * settings file is for: DT_NODE_FULL_NAME is a node's own name and not its
 * path, so devicetree keeps it unique only among siblings.
 */
#define ZMK_INPUT_VECTOR_ACCELERATION_SETTING_KEY(n, field)                                        \
    DT_NODE_FULL_NAME(DT_DRV_INST(n)) "." field

/*
 * The name a setting is stored under, which is longer than its key.
 *
 * custom-settings prefixes its own subtree and the subsystem before saving:
 * setting_storage_name() builds "custom_settings/<subsystem>/<key>" into a
 * SETTINGS_MAX_NAME_LEN buffer and returns -ENAMETOOLONG if it does not fit.
 * "custom_settings" is private to that module, so it is spelled out here; if
 * it ever changes, this over-estimates or under-estimates the budget and the
 * assert below is the thing to fix.
 */
#define ZMK_INPUT_VECTOR_ACCELERATION_STORAGE_NAME(n, field)                                       \
    "custom_settings/" ZMK_INPUT_VECTOR_ACCELERATION_SUBSYSTEM                                     \
    "/" ZMK_INPUT_VECTOR_ACCELERATION_SETTING_KEY(n, field)

/*
 * Fail by name, at build time, when a node cannot fit a settings key.
 *
 * There are two limits and they are not the same one. custom-settings refuses
 * a key over CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN (48) at build time, which
 * is the Studio RPC protobuf's limit on the key alone. Zephyr's settings
 * subsystem separately refuses a *stored name* over SETTINGS_MAX_NAME_LEN
 * (64), which covers the subtree and the subsystem too -- and it refuses it at
 * runtime, inside the save path, long after the value has been accepted over
 * RPC and applied to the hardware. The setting reads back correctly for as
 * long as the keyboard stays powered and is simply gone after a reboot.
 *
 * Nothing in custom-settings checks the two together, and they are not jointly
 * satisfiable: 48 + 32 + the prefixes is well past 64. So the second limit is
 * checked here, where the node that caused it can be named.
 *
 * The longest field is "unity_speed" at 11, which with
 * "amgskobo__accel" leaves a node 19 characters.
 */
#define ZMK_INPUT_VECTOR_ACCELERATION_ASSERT_NAME_FITS(n, longest_field)                           \
    BUILD_ASSERT(sizeof(ZMK_INPUT_VECTOR_ACCELERATION_SETTING_KEY(n, longest_field)) <=            \
                     CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN,                                       \
                 "devicetree node \"" DT_NODE_FULL_NAME(DT_DRV_INST(                               \
                     n)) "\" has a name too long to key its settings; shorten the node name");     \
    BUILD_ASSERT(                                                                                  \
        sizeof(ZMK_INPUT_VECTOR_ACCELERATION_STORAGE_NAME(n, longest_field)) <=                    \
            SETTINGS_MAX_NAME_LEN,                                                                 \
        "devicetree node \"" DT_NODE_FULL_NAME(DT_DRV_INST(                                        \
            n)) "\" has a name too long to store its settings under; shorten the node name");
