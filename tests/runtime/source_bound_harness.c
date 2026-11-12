/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zmk-input-vector-acceleration/vector_accel_core.h>

struct zmk_input_processor_state { uint8_t input_device_index; int16_t *remainder; };
struct vector_accel_data {
    struct vector_accel_stream streams[VECTOR_ACCEL_STREAM_COUNT];
};

/* DRIVER_FUNCTIONS */

int main(void) {
    struct vector_accel_data first = {0}, second = {0};
    size_t index = 999U;
    for (unsigned int i = 0; i < VECTOR_ACCEL_STREAM_COUNT; i++) {
        struct zmk_input_processor_state state = {.input_device_index = (uint8_t)i};
        first.streams[i].history_magnitude = i + 1U;
        second.streams[i].history_magnitude = i + 1000U;
        assert(stream_for_event(&first, &state, &index) == &first.streams[i]);
        assert(index == i && first.streams[index].history_magnitude == i + 1U);
        assert(stream_for_event(&second, &state, &index) == &second.streams[i]);
        assert(index == i && second.streams[index].history_magnitude == i + 1000U);
    }
    assert(stream_for_event(&first, NULL, &index) == &first.streams[0]);
    assert(index == 0U);
#if VECTOR_ACCEL_STREAM_COUNT < 256
    for (unsigned int i = VECTOR_ACCEL_STREAM_COUNT; i <= UINT8_MAX; i++) {
        struct zmk_input_processor_state state = {.input_device_index = (uint8_t)i};
        index = 999U;
        assert(stream_for_event(&first, &state, &index) == NULL);
        assert(index == 999U);
    }
#endif
    printf("vector source prefix %u: all 256 IDs, two nodes and NULL PASS\n",
           (unsigned int)VECTOR_ACCEL_STREAM_COUNT);
    return 0;
}
