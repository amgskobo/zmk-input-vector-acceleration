/* SPDX-License-Identifier: MIT */
#include VECTOR_ACCEL_TEST_DRIVER_SOURCE
BUILD_ASSERT(ARRAY_SIZE(((struct vector_accel_data *)0)->streams) == VECTOR_ACCEL_TEST_SOURCES,
             "vector source capacity allocation mismatch");
