/* SPDX-License-Identifier: MIT */

/*
 * ESP32 classic simulated-observation validation example.
 *
 * This program does not access physical sensors, GPIO, ADC, I2C, SPI, or any
 * other board peripheral. It creates a logical sensor and generates
 * deterministic observations in memory so the Aisthra engine, ESP-IDF
 * integration, heap probes, lifecycle checks, and concurrency checks can be
 * validated in QEMU or on an ESP32 board without external hardware.
 *
 * The values published below are test fixtures, not measurements from a real
 * temperature sensor. A hardware application must replace these simulated
 * values with its own acquisition callback and peripheral integration.
 */


#include <aisthra/advanced.h>

#include "../../../../tests/embedded/public_benchmark.h"
#include "../../../../tests/embedded/public_contract.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_IDF_TARGET
#define CONFIG_IDF_TARGET "unknown"
#endif

#if defined(CONFIG_FREERTOS_UNICORE)
#define AISTHRA_ESP32_UNICORE 1
#else
#define AISTHRA_ESP32_UNICORE 0
#endif

typedef struct smoke_provider_state {
    ais_sensor_id_t sensor;
    ais_provider_id_t provider;
    uint32_t starts;
    uint32_t stops;
    uint32_t polls;
    uint32_t destroys;
} smoke_provider_state_t;

typedef struct publish_worker_state {
    ais_ctx_t *ctx;
    ais_sensor_id_t sensor;
    uint32_t offset;
    uint32_t failures;
} publish_worker_state_t;

typedef struct smoke_event_state {
    atomic_uint accepted;
    atomic_uint availability;
} smoke_event_state_t;

static void heap_checkpoint(const char *name)
{
    printf(
        "AISTHRA_EMBEDDED_HEAP checkpoint=%s free=%u minimum=%u largest=%u\n",
        name,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    printf(
        "AISTHRA_EMBEDDED_STACK checkpoint=%s high_water_bytes=%u\n",
        name, (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void heap_pressure_probe(void)
{
    enum { BLOCK_COUNT = 8 };
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    void *blocks[BLOCK_COUNT] = {0};
    void *large_block = NULL;
    size_t free_before = heap_caps_get_free_size(caps);
    size_t largest_before = heap_caps_get_largest_free_block(caps);
    void *oversized = NULL;
    size_t fragmented_largest;

    if (free_before < SIZE_MAX) {
        oversized = heap_caps_malloc(free_before + 1u, caps);
    }
    if (largest_before >= 8192u) {
        large_block = heap_caps_malloc(largest_before / 2u, caps);
    }
    for (uint32_t index = 0u; index < BLOCK_COUNT; ++index) {
        blocks[index] = heap_caps_malloc(1024u + index * 257u, caps);
    }
    for (uint32_t index = 1u; index < BLOCK_COUNT; index += 2u) {
        heap_caps_free(blocks[index]);
        blocks[index] = NULL;
    }
    fragmented_largest = heap_caps_get_largest_free_block(caps);
    for (uint32_t index = 0u; index < BLOCK_COUNT; ++index) {
        if (blocks[index] != NULL) {
            heap_caps_free(blocks[index]);
        }
    }
    if (large_block != NULL) {
        heap_caps_free(large_block);
    }
    if (oversized != NULL) {
        heap_caps_free(oversized);
    }
    printf(
        "AISTHRA_EMBEDDED_HEAP_PROBE largest_before=%u fragmented_largest=%u "
        "large_block=%s oom=%s\n",
        (unsigned)largest_before,
        (unsigned)fragmented_largest,
        large_block == NULL ? "not-observed" : "observed",
        oversized == NULL ? "observed" : "not-observed");
}

static int require_status(
    ais_status_t actual, ais_status_t expected, const char *operation)
{
    if (actual != expected) {
        printf(
            "AISTHRA_EMBEDDED_SMOKE:FAIL operation=%s expected=%s actual=%s\n",
            operation, ais_status_name(expected), ais_status_name(actual));
        return 0;
    }
    return 1;
}

static int require_condition(int condition, const char *operation)
{
    if (!condition) {
        printf("AISTHRA_EMBEDDED_SMOKE:FAIL operation=%s\n", operation);
        return 0;
    }
    return 1;
}

static double embedded_clock_now(void)
{
    const int64_t microseconds = esp_timer_get_time();
    return microseconds < 0 ? -1.0 : (double)microseconds / 1000000.0;
}

static ais_status_t provider_start(void *user_data)
{
    smoke_provider_state_t *state = (smoke_provider_state_t *)user_data;
    if (state == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    state->starts += 1u;
    return AIS_OK;
}

static ais_status_t provider_stop(void *user_data)
{
    smoke_provider_state_t *state = (smoke_provider_state_t *)user_data;
    if (state == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    state->stops += 1u;
    return AIS_OK;
}

static ais_status_t provider_poll(
    void *user_data, ais_provider_emit_fn emit, void *emit_context)
{
    /*
     * The provider callback is deliberately synthetic. The fixed 24.25 value
     * represents a simulated temperature observation for contract testing.
     */
    smoke_provider_state_t *state = (smoke_provider_state_t *)user_data;
    ais_observation_view_t observation;
    ais_ingest_result_t result;
    ais_error_info_t error;

    if (state == NULL || emit == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    ais_observation_view_init(&observation);
    ais_ingest_result_init(&result);
    ais_error_info_init(&error);
    observation.sensor_id = state->sensor;
    observation.provider_id = state->provider;
    observation.acquisition = AIS_ACQUISITION_POINT;
    observation.observation_type = AIS_SV("environment.temperature.v1");
    observation.unit_code = AIS_UNIT_CELSIUS;
    observation.reference_frame_code = AIS_REF_DEVICE;
    observation.provenance.source_type = AIS_SV("aisthra.embedded.smoke");
    if (ais_payload_set_real(&observation.payload, 24.25) != AIS_OK) {
        return AIS_E_INTERNAL;
    }
    if (emit(emit_context, &observation, &result, &error) == AIS_OK) {
        state->polls += 1u;
        return AIS_OK;
    }
    return error.code == AIS_OK ? AIS_E_INTERNAL : error.code;
}

static void provider_destroy(void *user_data)
{
    smoke_provider_state_t *state = (smoke_provider_state_t *)user_data;
    if (state != NULL) {
        state->destroys += 1u;
    }
}

static void event_callback(void *user_data, const ais_event_view_t *event)
{
    smoke_event_state_t *state = (smoke_event_state_t *)user_data;
    if (state == NULL || event == NULL) {
        return;
    }
    if (event->type == AIS_EVENT_ACCEPTED) {
        (void)atomic_fetch_add_explicit(
            &state->accepted, 1u, memory_order_relaxed);
    }
    if (event->type == AIS_EVENT_PROVIDER_AVAILABILITY) {
        (void)atomic_fetch_add_explicit(
            &state->availability, 1u, memory_order_relaxed);
    }
}

static void *publish_worker(void *argument)
{
    publish_worker_state_t *state = (publish_worker_state_t *)argument;
    if (state == NULL) {
        return NULL;
    }
    for (uint32_t index = 0u; index < 64u; ++index) {
        ais_ingest_result_t result;
        ais_error_info_t error;
        ais_ingest_result_init(&result);
        ais_error_info_init(&error);
        if (ais_publish_f64(
                state->ctx, state->sensor,
                (double)(state->offset + index), &result, &error) != AIS_OK) {
            state->failures += 1u;
        }
    }
    printf(
        "AISTHRA_EMBEDDED_THREAD offset=%u high_water_bytes=%u failures=%u\n",
        (unsigned)state->offset,
        (unsigned)uxTaskGetStackHighWaterMark(NULL),
        (unsigned)state->failures);
    return NULL;
}

int aisthra_embedded_run_parallel(ais_ctx_t *ctx, ais_sensor_id_t sensor)
{
    pthread_t threads[2];
    publish_worker_state_t workers[2];
    uint8_t thread_started[2] = {0u, 0u};
    int failures = 0;

    for (uint32_t index = 0u; index < 2u; ++index) {
        workers[index].ctx = ctx;
        workers[index].sensor = sensor;
        workers[index].offset = 3000u + index * 100u;
        workers[index].failures = 0u;
        if (pthread_create(
                &threads[index], NULL, publish_worker, &workers[index]) != 0) {
            failures += 1;
            continue;
        }
        thread_started[index] = 1u;
    }
    for (uint32_t index = 0u; index < 2u; ++index) {
        if (thread_started[index] != 0u &&
            pthread_join(threads[index], NULL) != 0) {
            failures += 1;
        }
        failures += (int)workers[index].failures;
    }
    return failures;
}

static int run_smoke(void)
{
    ais_ctx_t *ctx = NULL;
    ais_ctx_t *recreated = NULL;
    ais_provider_config_t provider_config;
    ais_provider_status_t provider_status;
    ais_sensor_config_t sensor_config;
    ais_sensor_status_t sensor_status;
    ais_state_summary_t summary;
    ais_state_snapshot_t *state_snapshot = NULL;
    ais_observation_snapshot_t *observation_snapshot = NULL;
    ais_observation_snapshot_t *snapshot_observation = NULL;
    ais_observation_view_t observation_view;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_provider_id_t provider = 0u;
    ais_sensor_id_t sensor = 0u;
    uint64_t subscription = 0u;
    smoke_provider_state_t provider_state = {0};
    smoke_event_state_t event_state;
    double latest = 0.0;
    size_t observation_count = 0u;
    size_t percept_count = 0u;
    uint8_t supported = 0u;
    pthread_t threads[2];
    publish_worker_state_t workers[2];
    uint8_t thread_started[2] = {0u, 0u};
    int ok = 0;

    atomic_init(&event_state.accepted, 0u);
    atomic_init(&event_state.availability, 0u);
    ais_error_info_init(&error);
    ais_provider_config_init(&provider_config);
    ais_provider_status_init(&provider_status);
    ais_sensor_config_init(&sensor_config);
    ais_sensor_status_init(&sensor_status);
    ais_state_summary_init(&summary);
    ais_observation_view_init(&observation_view);
    ais_ingest_result_init(&result);

    printf(
        "AISTHRA_EMBEDDED_CONFIG target=%s cpu_mhz=%d unicore=%d "
        "pthread_stack=%d psram=not-required\n",
        CONFIG_IDF_TARGET,
        CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        AISTHRA_ESP32_UNICORE,
        CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT);
    printf(
        "AISTHRA_EMBEDDED_ABI pointer=%u size_t=%u long=%u uint64=%u "
        "atomic_int_lock_free=%d atomic_long_lock_free=%d "
        "atomic_llong_lock_free=%d atomic_pointer_lock_free=%d\n",
        (unsigned)sizeof(void *),
        (unsigned)sizeof(size_t),
        (unsigned)sizeof(long),
        (unsigned)sizeof(uint64_t),
        ATOMIC_INT_LOCK_FREE,
        ATOMIC_LONG_LOCK_FREE,
        ATOMIC_LLONG_LOCK_FREE,
        ATOMIC_POINTER_LOCK_FREE);
    heap_checkpoint("initial");
    heap_pressure_probe();
    heap_checkpoint("after-heap-probe");

    if (!require_status(
            ais_ctx_create_default(&ctx, &error), AIS_OK, "ctx-create") ||
        !require_condition(ctx != NULL, "ctx-created")) {
        goto cleanup;
    }
    heap_checkpoint("after-context");

    provider_config.key = AIS_SV("aisthra.esp32.provider");
    provider_config.version = AIS_SV("1.0");
    provider_config.vtable.start = provider_start;
    provider_config.vtable.stop = provider_stop;
    provider_config.vtable.poll = provider_poll;
    provider_config.vtable.destroy = provider_destroy;
    provider_config.user_data = &provider_state;
    if (!require_status(
            ais_provider_register(
                ctx, &provider_config, &provider, &error),
            AIS_OK, "provider-register")) {
        goto cleanup;
    }
    provider_state.provider = provider;

    sensor_config.key = AIS_SV("esp32-temperature");
    sensor_config.schema = AIS_SV("environment.temperature.v1");
    sensor_config.unit = AIS_SV("celsius");
    sensor_config.reference_frame = AIS_SV("device");
    sensor_config.provider_id = provider;
    if (!require_status(
            ais_sensor_register(
                ctx, &sensor_config, &sensor, &error),
            AIS_OK, "sensor-register")) {
        goto cleanup;
    }
    provider_state.sensor = sensor;
    heap_checkpoint("after-registration");

    if (!require_status(
            ais_event_subscribe(
                ctx, AIS_EVENT_ACCEPTED | AIS_EVENT_PROVIDER_AVAILABILITY,
                event_callback, &event_state, NULL, &subscription, &error),
            AIS_OK, "event-subscribe") ||
        !require_status(
            ais_provider_start(ctx, provider, &error), AIS_OK,
            "provider-start") ||
        !require_condition(provider_state.starts == 1u, "provider-started")) {
        goto cleanup;
    }

    observation_view.sensor_id = sensor;
    if (!require_status(
            ais_publish_f64(ctx, sensor, 23.5, &result, &error), AIS_OK,
            "publish-valid") ||
        !require_condition(
            result.disposition == AIS_INGEST_ACCEPTED &&
                result.sequence == 1u,
            "publish-result") ||
        !require_status(
            ais_latest_f64(ctx, sensor, &latest, &error), AIS_OK,
            "latest") ||
        !require_condition(latest == 23.5, "latest-value") ||
        !require_status(
            ais_observation_validate(ctx, &observation_view, &error),
            AIS_E_INVALID_OBSERVATION, "invalid-observation")) {
        goto cleanup;
    }
    heap_checkpoint("after-publication");

    if (!require_status(
            ais_sensor_latest_observation(
                ctx, sensor, &observation_snapshot, &error),
            AIS_OK, "observation-snapshot") ||
        !require_status(
            ais_observation_snapshot_get_view(
                observation_snapshot, &observation_view, &error),
            AIS_OK, "observation-view") ||
        !require_condition(
            observation_view.payload.kind == AIS_PAYLOAD_REAL,
            "observation-payload") ||
        !require_status(
            ais_observation_snapshot_release(
                &observation_snapshot, &error),
            AIS_OK, "observation-release")) {
        goto cleanup;
    }

    if (!require_status(
            ais_ctx_refresh(ctx, &error), AIS_OK, "refresh") ||
        !require_status(
            ais_ctx_get_state_summary(ctx, &summary, &error), AIS_OK,
            "state-summary") ||
        !require_condition(summary.recent_observation_count >= 1u, "history") ||
        !require_status(
            ais_ctx_state_snapshot(ctx, &state_snapshot, &error), AIS_OK,
            "state-snapshot") ||
        !require_status(
            ais_state_snapshot_observation_count(
                state_snapshot, &observation_count, &error),
            AIS_OK, "snapshot-observation-count") ||
        !require_condition(observation_count >= 1u, "snapshot-history") ||
        !require_status(
            ais_state_snapshot_observation_at(
                state_snapshot, 0u, &snapshot_observation, &error),
            AIS_OK, "snapshot-observation-at") ||
        !require_status(
            ais_observation_snapshot_release(
                &snapshot_observation, &error),
            AIS_OK, "snapshot-observation-release") ||
        !require_status(
            ais_state_snapshot_percept_count(
                state_snapshot, &percept_count, &error),
            AIS_OK, "snapshot-percept-count") ||
        !require_status(
            ais_state_snapshot_release(&state_snapshot, &error),
            AIS_OK, "state-snapshot-release")) {
        goto cleanup;
    }

    if (!require_status(
            ais_latest_f64(ctx, UINT64_C(0xffff), &latest, &error),
            AIS_E_UNKNOWN_SENSOR, "invalid-sensor") ||
        !require_status(
            ais_feature_supported(0u, &supported), AIS_OK,
            "feature-query")) {
        goto cleanup;
    }

    if (!require_status(
            ais_provider_poll(ctx, provider, &error), AIS_OK,
            "provider-poll") ||
        !require_condition(provider_state.polls == 1u, "provider-emission") ||
        !require_status(
            ais_provider_get_status(
                ctx, provider, &provider_status, &error),
            AIS_OK, "provider-status") ||
        !require_condition(provider_status.observation_count >= 1u, "provider-count")) {
        goto cleanup;
    }

    for (uint32_t index = 0u; index < 2u; ++index) {
        workers[index].ctx = ctx;
        workers[index].sensor = sensor;
        workers[index].offset = 1000u + index * 100u;
        workers[index].failures = 0u;
        if (pthread_create(
                &threads[index], NULL, publish_worker, &workers[index]) != 0) {
            printf(
                "AISTHRA_EMBEDDED_SMOKE:FAIL operation=thread-create index=%u\n",
                (unsigned)index);
            goto cleanup;
        }
        thread_started[index] = 1u;
    }
    for (uint32_t index = 0u; index < 2u; ++index) {
        if (thread_started[index] != 0u &&
            pthread_join(threads[index], NULL) != 0) {
            printf(
                "AISTHRA_EMBEDDED_SMOKE:FAIL operation=thread-join index=%u\n",
                (unsigned)index);
            goto cleanup;
        }
        thread_started[index] = 0u;
        if (!require_condition(
                workers[index].failures == 0u, "concurrent-publication")) {
            goto cleanup;
        }
    }
    heap_checkpoint("after-concurrency");

    {
        const int64_t begin = esp_timer_get_time();
        for (uint32_t index = 0u; index < 128u; ++index) {
            ais_ingest_result_init(&result);
            ais_error_info_init(&error);
            if (!require_status(
                    ais_publish_f64(
                        ctx, sensor, (double)(2000u + index), &result, &error),
                    AIS_OK, "stress-publish")) {
                goto cleanup;
            }
        }
        const int64_t elapsed = esp_timer_get_time() - begin;
        printf(
            "AISTHRA_EMBEDDED_METRIC workload=publish count=128 elapsed_us=%lld avg_us=%.3f\n",
            (long long)elapsed, (double)elapsed / 128.0);
    }
    heap_checkpoint("after-stress");

    if (!require_status(
            ais_provider_set_availability(
                ctx, provider, AIS_AVAIL_DEGRADED,
                AIS_E_PROVIDER_UNAVAILABLE, &error),
            AIS_OK, "provider-degraded") ||
        !require_status(
            ais_provider_stop(ctx, provider, &error), AIS_OK,
            "provider-stop") ||
        !require_status(
            ais_sensor_unregister(ctx, sensor, &error), AIS_OK,
            "sensor-unregister") ||
        !require_status(
            ais_provider_unregister(ctx, provider, &error), AIS_OK,
            "provider-unregister") ||
        !require_status(
            ais_event_unsubscribe(ctx, subscription, &error), AIS_OK,
            "event-unsubscribe")) {
        goto cleanup;
    }
    provider = 0u;
    sensor = 0u;
    subscription = 0u;
    if (!require_status(
            ais_ctx_close(&ctx, &error), AIS_OK, "ctx-close") ||
        !require_condition(ctx == NULL, "ctx-closed")) {
        goto cleanup;
    }
    heap_checkpoint("after-close");

    if (!require_status(
            ais_ctx_create_default(&recreated, &error), AIS_OK,
            "ctx-recreate") ||
        !require_status(
            ais_ctx_close(&recreated, &error), AIS_OK,
            "ctx-recreate-close") ||
        !require_condition(recreated == NULL, "ctx-recreated")) {
        goto cleanup;
    }
    ok = 1;

cleanup:
    for (uint32_t index = 0u; index < 2u; ++index) {
        if (thread_started[index] != 0u) {
            (void)pthread_join(threads[index], NULL);
        }
    }
    if (snapshot_observation != NULL) {
        (void)ais_observation_snapshot_release(
            &snapshot_observation, &error);
    }
    if (state_snapshot != NULL) {
        (void)ais_state_snapshot_release(&state_snapshot, &error);
    }
    if (observation_snapshot != NULL) {
        (void)ais_observation_snapshot_release(
            &observation_snapshot, &error);
    }
    if (ctx != NULL) {
        (void)ais_ctx_close(&ctx, &error);
    }
    if (recreated != NULL) {
        (void)ais_ctx_close(&recreated, &error);
    }
    return ok;
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    if (aisthra_embedded_public_contract(aisthra_embedded_run_parallel) &&
        aisthra_embedded_public_benchmark(embedded_clock_now) &&
        run_smoke()) {
        printf("AISTHRA_EMBEDDED_RESULT target=%s status=PASS\n",
               CONFIG_IDF_TARGET);
        printf("AISTHRA_EMBEDDED_SMOKE:PASS\n");
#if defined(CONFIG_IDF_TARGET_ESP32)
        printf("AISTHRA_ESP32_SMOKE:PASS\n");
#endif
        return;
    }
    printf("AISTHRA_EMBEDDED_RESULT target=%s status=FAIL\n",
           CONFIG_IDF_TARGET);
    printf("AISTHRA_EMBEDDED_SMOKE:FAIL\n");
    abort();
}
