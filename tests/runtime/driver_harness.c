/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zmk-input-vector-acceleration/vector_accel_core.h>

typedef int k_spinlock_key_t;
typedef struct { int64_t ms; } k_timeout_t;
typedef ssize_t (*settings_read_cb)(void *cb_arg, void *data, size_t len);
struct k_spinlock { int held; };
struct k_work { void (*handler)(struct k_work *work); };
struct k_work_delayable { struct k_work work; bool scheduled; int64_t delay_ms; };
struct k_work_q { int unused; };
struct device { const void *config; void *data; const char *name; };
struct input_event { uint8_t sync; uint16_t type; uint16_t code; int32_t value; };
struct zmk_input_processor_state { uint8_t input_device_index; int16_t *remainder; };

#define IS_ENABLED(option) option
#define VECTOR_ACCEL_OWNS_PERSISTENCE 1
#define VECTOR_ACCEL_STREAM_COUNT 2
#define VECTOR_ACCEL_SETTINGS_ROOT "vaccel"
#define CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE 60000
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ARG_UNUSED(x) ((void)(x))
#define CONTAINER_OF(ptr, type, field) ((type *)(void *)((char *)(ptr) - offsetof(type, field)))
/* Firmware builds without CONFIG_ASSERT; kept unevaluated but type-checked. */
#define __ASSERT(cond, msg) ((void)sizeof((cond) && (msg)))
#define K_MSEC(ms) ((k_timeout_t){(ms)})
#define INPUT_EV_KEY 0x01
#define INPUT_EV_REL 0x02
#define INPUT_REL_X 0x00
#define INPUT_REL_Y 0x01
#define INPUT_REL_WHEEL 0x08
#define ZMK_INPUT_PROC_CONTINUE 0

struct vector_accel_data {
    struct vector_accel_stream streams[VECTOR_ACCEL_STREAM_COUNT];
    int16_t fallback_remainders[VECTOR_ACCEL_STREAM_COUNT][VECTOR_ACCEL_AXIS_COUNT];
    struct k_spinlock config_lock;
    struct vector_accel_config config;
    struct k_work_delayable save_work;
};

static struct device first_dev;
static struct device second_dev;
static const struct device *const vector_accel_devices[] = {&first_dev, &second_dev};
static struct k_work_q lowprio_queue;
static int64_t now_ms;
static int reschedule_result = 1;
static int subsys_result;
static char saved_key[32];
static struct vector_accel_config saved;
static int saves;
static const char *loaded_subtree;

static int clock_reads;
static int64_t k_uptime_get(void) {
    clock_reads++;
    return now_ms;
}
static k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = 1;
    return 9;
}
static void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held && key == 9);
    lock->held = 0;
}
static struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio_queue; }
static void k_work_init_delayable(struct k_work_delayable *work,
                                  void (*handler)(struct k_work *work)) {
    *work = (struct k_work_delayable){.work = {.handler = handler}};
}
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
static int k_work_reschedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                                       k_timeout_t delay) {
    assert(queue == &lowprio_queue);
    work->scheduled = reschedule_result >= 0;
    work->delay_ms = delay.ms;
    return reschedule_result;
}
/* Length of the first path segment; *next is what follows its slash, if any. */
static int settings_name_next(const char *name, const char **next) {
    const char *slash = strchr(name, '/');
    *next = slash != NULL ? slash + 1 : NULL;
    return slash != NULL ? (int)(slash - name) : (int)strlen(name);
}
static int settings_save_one(const char *name, const void *value, size_t len) {
    assert(len == sizeof(saved));
    snprintf(saved_key, sizeof(saved_key), "%s", name);
    memcpy(&saved, value, len);
    saves++;
    return 0;
}
static int settings_subsys_init(void) { return subsys_result; }
static int settings_load_subtree(const char *subtree) {
    loaded_subtree = subtree;
    return 0;
}
static void vector_accel_save_work_handler(struct k_work *work);

/* DRIVER_FUNCTIONS */

static struct vector_accel_data first, second;
static const struct vector_accel_config defaults = {
    .min_factor = 500, .max_factor = 3000, .unity_speed = 100, .max_speed = 1000};

/* A settings record handed to the set handler, as the backend would. */
struct record { const void *data; size_t len; ssize_t result; };
static ssize_t read_record(void *cb_arg, void *data, size_t len) {
    const struct record *record = cb_arg;
    memcpy(data, record->data, MIN(len, record->len));
    return record->result;
}

static int stored(const char *name, const struct vector_accel_config *config, size_t len,
                  ssize_t result) {
    struct record record = {config, len, result};
    return vector_accel_settings_set(name, len, read_record, &record);
}

static int32_t send(struct zmk_input_processor_state *state, uint16_t type, uint16_t code,
                    int32_t value, bool sync) {
    struct input_event event = {.sync = sync, .type = type, .code = code, .value = value};
    assert(vector_accel_handle_event(&first_dev, &event, 0, 0, state) == ZMK_INPUT_PROC_CONTINUE);
    assert(event.type == type && event.code == code);
    return event.value;
}

static void test_events(void) {
    int16_t remainder = 0;
    struct zmk_input_processor_state with_remainder = {.input_device_index = 1,
                                                       .remainder = &remainder};
    struct zmk_input_processor_state without = {.input_device_index = 1};
    struct zmk_input_processor_state stranger = {.input_device_index = VECTOR_ACCEL_STREAM_COUNT};

    /* Past the listeners, or not pointer motion: untouched. */
    assert(send(&stranger, INPUT_EV_REL, INPUT_REL_X, 40, true) == 40);
    assert(send(NULL, INPUT_EV_REL, INPUT_REL_WHEEL, 3, false) == 3);
    assert(send(NULL, INPUT_EV_KEY, INPUT_REL_X, 1, true) == 1); /* a sync, no frame open */
    /* A key whose code happens to be an axis's is not motion. */
    assert(send(NULL, INPUT_EV_KEY, INPUT_REL_X, 1, false) == 1);
    assert(send(NULL, INPUT_EV_KEY, INPUT_REL_Y, 1, false) == 1);
    assert(!first.streams[0].frame_open && clock_reads == 0);

    /* Slow frames run below unity; each stream keeps its own remainder. */
    now_ms = 1000;
    assert(send(NULL, INPUT_EV_REL, INPUT_REL_X, 1, false) >= 0);
    assert(send(NULL, INPUT_EV_REL, INPUT_REL_Y, 1, true) >= 0);
    assert(!first.streams[0].frame_open);
    now_ms = 1010;
    int32_t moved = 0;
    for (int i = 0; i < 4; i++) {
        moved += send(NULL, INPUT_EV_REL, INPUT_REL_X, 1, false);
        send(NULL, INPUT_EV_REL, INPUT_REL_Y, 0, true);
        now_ms += 10;
    }
    assert(moved >= 1 && moved <= 4);

    /* A listener's own remainder is used when it has one... */
    now_ms += 10;
    send(&with_remainder, INPUT_EV_REL, INPUT_REL_X, 1, false);
    send(&with_remainder, INPUT_EV_REL, INPUT_REL_Y, 1, true);
    /* ...and the processor's per-stream fallback when it has not. */
    now_ms += 10;
    send(&without, INPUT_EV_REL, INPUT_REL_Y, 1, true);
    for (int i = 0; i < 2; i++) {
        for (int axis = 0; axis < VECTOR_ACCEL_AXIS_COUNT; axis++) {
            assert(first.fallback_remainders[i][axis] > -1000 &&
                   first.fallback_remainders[i][axis] < 1000);
        }
    }

    /* A frame closed by a sync that carries no motion of its own. */
    now_ms += 10;
    send(NULL, INPUT_EV_REL, INPUT_REL_X, 5, false);
    assert(first.streams[0].frame_open);
    const int reads = clock_reads;
    send(NULL, INPUT_EV_KEY, 0, 0, false); /* neither motion nor a sync */
    assert(clock_reads == reads && first.streams[0].frame_open);
    send(NULL, INPUT_EV_KEY, 0, 0, true);
    assert(!first.streams[0].frame_open);

    /* A fast flick is scaled up past unity. */
    now_ms += 10;
    send(NULL, INPUT_EV_REL, INPUT_REL_X, 200, true);
    now_ms += 10;
    assert(send(NULL, INPUT_EV_REL, INPUT_REL_X, 200, true) > 200);
}

/*
 * Each event is scaled on its own stream with the factor that stream has
 * built, and its fraction kept where the listener asked: in the listener's
 * slot, else in the processor's own. The core is the oracle.
 */
static void test_scaling_wiring(void) {
    int16_t slot = 0;
    struct zmk_input_processor_state with_slot = {.input_device_index = 1, .remainder = &slot};
    struct zmk_input_processor_state without = {.input_device_index = 1};
    struct vector_accel_stream *s = &first.streams[1];
    const int64_t stream0_time = first.streams[0].last_report_time_ms;

    /* Two slow frames on stream 1: one count per 20 ms is below unity. */
    now_ms = 5000;
    send(&without, INPUT_EV_REL, INPUT_REL_X, 1, true);
    now_ms = 5020;
    send(&without, INPUT_EV_REL, INPUT_REL_X, 1, true);
    assert(s->have_report_time && s->last_report_time_ms == 5020);
    assert(first.streams[0].last_report_time_ms == stream0_time);
    const uint16_t slow = s->factor;
    assert(slow < VECTOR_ACCEL_SCALE);

    /* The listener's slot: X, then Y closing the frame. */
    now_ms = 5040;
    slot = 700;
    int32_t fraction = slot;
    int32_t want = vector_accel_scale_value(3, slow, &fraction);
    assert(want != 3);
    assert(send(&with_slot, INPUT_EV_REL, INPUT_REL_X, 3, false) == want && slot == fraction);
    want = vector_accel_scale_value(5, slow, &fraction);
    assert(want != 5);
    assert(send(&with_slot, INPUT_EV_REL, INPUT_REL_Y, 5, true) == want && slot == fraction);

    /* The processor's own slot, per stream and axis. */
    now_ms = 5060;
    const uint16_t factor = s->factor;
    first.fallback_remainders[1][VECTOR_ACCEL_AXIS_X] = 300;
    fraction = 300;
    want = vector_accel_scale_value(3, factor, &fraction);
    assert(send(&without, INPUT_EV_REL, INPUT_REL_X, 3, false) == want);
    assert(first.fallback_remainders[1][VECTOR_ACCEL_AXIS_X] == fraction);
    send(&without, INPUT_EV_REL, INPUT_REL_Y, 0, true);
}

static void test_api(void) {
    struct device stranger = {.config = &defaults, .data = &first};
    struct vector_accel_config out;
    struct vector_accel_config faster = defaults;
    faster.max_factor = 5000;

    assert(vector_accel_get_config(NULL, &out) == -EINVAL);
    assert(vector_accel_get_config(&first_dev, NULL) == -EINVAL);
    assert(vector_accel_get_config(&stranger, &out) == -ENODEV);
    assert(vector_accel_get_config(&first_dev, &out) == 0 && out.max_factor == 3000);
    assert(vector_accel_get_config(&second_dev, &out) == 0 && out.max_factor == 3000);

    assert(vector_accel_set_config(NULL, &faster) == -EINVAL);
    assert(vector_accel_set_config(&second_dev, NULL) == -EINVAL);
    assert(vector_accel_set_config(&stranger, &faster) == -ENODEV);

    /* A write applies at once and saves later, from the work queue. */
    assert(vector_accel_set_config(&second_dev, &faster) == 0);
    assert(second.config.max_factor == 5000 && second.save_work.scheduled);
    assert(second.save_work.delay_ms == CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE && saves == 0);
    second.save_work.work.handler(&second.save_work.work);
    assert(saves == 1 && strcmp(saved_key, "vaccel/1") == 0 && saved.max_factor == 5000);

    /* A queue that refuses the save reports it; the value still applies. */
    reschedule_result = -EINVAL;
    faster.max_factor = 6000;
    assert(vector_accel_set_config(&first_dev, &faster) == -EINVAL);
    assert(first.config.max_factor == 6000);
    reschedule_result = 1;

    /* Work whose data no device owns stores nothing. */
    struct vector_accel_data orphan = {0};
    k_work_init_delayable(&orphan.save_work, vector_accel_save_work_handler);
    orphan.config = defaults;
    orphan.save_work.work.handler(&orphan.save_work.work);
    assert(saves == 1);

    /* The first device saves under its own index. */
    assert(vector_accel_set_config(&first_dev, &faster) == 0);
    first.save_work.work.handler(&first.save_work.work);
    assert(saves == 2 && strcmp(saved_key, "vaccel/0") == 0);
}

static void test_stored_values(void) {
    struct vector_accel_config value = defaults;
    struct vector_accel_config invalid = defaults;
    invalid.max_speed = invalid.unity_speed;
    value.min_factor = 700;

    /* Names that are not one instance index are not ours. */
    assert(stored("", &value, sizeof(value), sizeof(value)) == -ENOENT);
    assert(stored("0/extra", &value, sizeof(value), sizeof(value)) == -ENOENT);
    assert(stored("x", &value, sizeof(value), sizeof(value)) == -ENOENT);
    assert(stored("1x", &value, sizeof(value), sizeof(value)) == -ENOENT);
    assert(stored("-1", &value, sizeof(value), sizeof(value)) == -ENOENT);
    assert(stored("2", &value, sizeof(value), sizeof(value)) == -ENOENT);
    assert(stored("99999999999999999999999", &value, sizeof(value), sizeof(value)) == -ENOENT);

    /* A record of the wrong shape, a short or failed read, or an invalid
     * curve is dropped and the defaults stay. */
    assert(stored("0", &value, sizeof(value) - 1, sizeof(value)) == 0);
    assert(stored("0", &value, sizeof(value), 3) == 0);
    assert(stored("0", &value, sizeof(value), -EIO) == -EIO);
    assert(stored("0", &invalid, sizeof(invalid), sizeof(invalid)) == 0);
    assert(first.config.min_factor == defaults.min_factor);

    /* A good one replaces them. */
    assert(stored("0", &value, sizeof(value), sizeof(value)) == 0);
    assert(first.config.min_factor == 700);

    /* Loading waits on the settings subsystem. */
    subsys_result = -EIO;
    assert(vector_accel_settings_load() == -EIO && loaded_subtree == NULL);
    subsys_result = 0;
    assert(vector_accel_settings_load() == 0 && strcmp(loaded_subtree, "vaccel") == 0);
}

int main(void) {
    first_dev = (struct device){.config = &defaults, .data = &first, .name = "first"};
    second_dev = (struct device){.config = &defaults, .data = &second, .name = "second"};
    second.streams[0].frame_open = second.streams[1].frame_open = true;
    assert(vector_accel_init(&first_dev) == 0 && vector_accel_init(&second_dev) == 0);
    assert(!second.streams[0].frame_open && !second.streams[1].frame_open);
    assert(first.config.unity_speed == 100 && first.save_work.work.handler != NULL);

    test_events();
    test_scaling_wiring();
    test_api();
    test_stored_values();
    assert(!first.config_lock.held && !second.config_lock.held);
    puts("vector-acceleration driver: PASS");
    return 0;
}
