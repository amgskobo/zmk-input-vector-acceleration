/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Host stand-in for the one Zephyr header the public API headers include.
 */

#pragma once

struct device {
    const char *name;
    const void *config;
    void *data;
};
