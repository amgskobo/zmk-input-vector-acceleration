/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime configuration for the vector acceleration processor.
 *
 * The devicetree values are the defaults. A processor that has been given a
 * runtime configuration keeps it in RAM. When CONFIG_SETTINGS is enabled,
 * either this driver or
 * the optional custom-settings integration owns its
 * persistence. The pure curve maths stays in
 * vector_accel_core.h, which
 * carries no Zephyr dependency so it can be unit tested on the host.

 */

#pragma once

#include <zephyr/device.h>

#include <zmk-input-vector-acceleration/vector_accel_core.h>

/* Reads the values the processor is applying right now. */
int vector_accel_get_config(const struct device *dev, struct vector_accel_config *out);

/*
 * Applies a new configuration. With CONFIG_SETTINGS enabled and without the
 * optional
 * custom-settings integration, the driver also persists it. When
 * custom-settings is enabled,
 * that registry is the sole persistence owner and
 * a direct call changes RAM only. Returns
 * -EINVAL and changes nothing when the
 * values fall outside the bounds the devicetree
 * BUILD_ASSERTs enforce, so a
 * bad value from a client cannot take a processor out of service.

 */
int vector_accel_set_config(const struct device *dev, const struct vector_accel_config *config);
