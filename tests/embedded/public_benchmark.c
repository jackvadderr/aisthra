/* SPDX-License-Identifier: MIT */

#include "public_benchmark.h"

#include <aisthra/advanced.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    AISTHRA_EMBEDDED_CONTEXT_OPERATIONS = 4,
    AISTHRA_EMBEDDED_REGISTRATION_OPERATIONS = 8,
#if defined(AISTHRA_EMBEDDED_BENCH_OPERATIONS)
    AISTHRA_EMBEDDED_PUBLICATION_OPERATIONS =
        AISTHRA_EMBEDDED_BENCH_OPERATIONS
#else
    AISTHRA_EMBEDDED_PUBLICATION_OPERATIONS = 32
#endif
};

typedef int (*benchmark_operation_fn)(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, size_t operations);

static ais_status_t benchmark_context_create(
    ais_ctx_t **out_context, ais_error_info_t *error)
{
    ais_ctx_config_t config;
    ais_status_t status = ais_ctx_config_init(&config);
#if defined(AISTHRA_EMBEDDED_MAX_RECENT)
    config.max_recent_observations = AISTHRA_EMBEDDED_MAX_RECENT;
#endif
    if (status != AIS_OK) {
        return status;
    }
    return ais_ctx_create(&config, out_context, error);
}

static int benchmark_context_create_close(
    ais_ctx_t *unused_ctx, ais_sensor_id_t unused_sensor, size_t operations)
{
    (void)unused_ctx;
    (void)unused_sensor;
    for (size_t index = 0u; index < operations; ++index) {
        ais_ctx_t *ctx = NULL;
        ais_error_info_t error;
        ais_error_info_init(&error);
        if (benchmark_context_create(&ctx, &error) != AIS_OK ||
            ais_ctx_close(&ctx, &error) != AIS_OK || ctx != NULL) {
            return 0;
        }
    }
    return 1;
}

static int benchmark_provider_registration(
    ais_ctx_t *ctx, ais_sensor_id_t unused_sensor, size_t operations)
{
    (void)unused_sensor;
    for (size_t index = 0u; index < operations; ++index) {
        ais_provider_config_t config;
        ais_provider_id_t provider = 0u;
        ais_error_info_t error;
        ais_provider_config_init(&config);
        ais_error_info_init(&error);
        config.key = AIS_SV("aisthra.embedded.benchmark.provider");
        config.version = AIS_SV("1.0");
        if (ais_provider_register(ctx, &config, &provider, &error) != AIS_OK ||
            ais_provider_unregister(ctx, provider, &error) != AIS_OK) {
            return 0;
        }
    }
    return 1;
}

static int benchmark_sensor_registration(
    ais_ctx_t *ctx, ais_sensor_id_t unused_sensor, size_t operations)
{
    (void)unused_sensor;
    for (size_t index = 0u; index < operations; ++index) {
        ais_sensor_config_t config;
        ais_sensor_id_t sensor = 0u;
        ais_error_info_t error;
        ais_sensor_config_init(&config);
        ais_error_info_init(&error);
        config.key = AIS_SV("aisthra.embedded.benchmark.sensor");
        config.schema = AIS_SV("environment.temperature.v1");
        config.unit = AIS_SV("celsius");
        config.reference_frame = AIS_SV("device");
        if (ais_sensor_register(ctx, &config, &sensor, &error) != AIS_OK ||
            ais_sensor_unregister(ctx, sensor, &error) != AIS_OK) {
            return 0;
        }
    }
    return 1;
}

static int benchmark_validation(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, size_t operations)
{
    for (size_t index = 0u; index < operations; ++index) {
        ais_observation_view_t observation;
        ais_error_info_t error;
        ais_observation_view_init(&observation);
        ais_error_info_init(&error);
        observation.sensor_id = sensor;
        if (ais_observation_validate(
                ctx, &observation, &error) != AIS_E_INVALID_OBSERVATION) {
            return 0;
        }
    }
    return 1;
}

static int benchmark_publication(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, size_t operations)
{
    for (size_t index = 0u; index < operations; ++index) {
        ais_ingest_result_t result;
        ais_error_info_t error;
        ais_ingest_result_init(&result);
        ais_error_info_init(&error);
        if (ais_publish_f64(
                ctx, sensor, (double)(1000u + index), &result, &error) !=
                AIS_OK) {
            return 0;
        }
    }
    return 1;
}

static int benchmark_latest(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, size_t operations)
{
    for (size_t index = 0u; index < operations; ++index) {
        ais_error_info_t error;
        double value = 0.0;
        ais_error_info_init(&error);
        if (ais_latest_f64(ctx, sensor, &value, &error) != AIS_OK) {
            return 0;
        }
    }
    return 1;
}

static int benchmark_snapshot(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, size_t operations)
{
    for (size_t index = 0u; index < operations; ++index) {
        ais_observation_snapshot_t *observation = NULL;
        ais_observation_view_t view;
        ais_error_info_t error;
        ais_observation_view_init(&view);
        ais_error_info_init(&error);
        if (ais_sensor_latest_observation(
                ctx, sensor, &observation, &error) != AIS_OK ||
            ais_observation_snapshot_get_view(
                observation, &view, &error) != AIS_OK ||
            ais_observation_snapshot_release(&observation, &error) != AIS_OK) {
            if (observation != NULL) {
                (void)ais_observation_snapshot_release(&observation, &error);
            }
            return 0;
        }
    }
    return 1;
}

static int benchmark_refresh(
    ais_ctx_t *ctx, ais_sensor_id_t unused_sensor, size_t operations)
{
    (void)unused_sensor;
    for (size_t index = 0u; index < operations; ++index) {
        ais_error_info_t error;
        ais_error_info_init(&error);
        if (ais_ctx_refresh(ctx, &error) != AIS_OK) {
            return 0;
        }
    }
    return 1;
}

static int benchmark_percept(
    ais_ctx_t *ctx, ais_sensor_id_t unused_sensor, size_t operations)
{
    (void)unused_sensor;
    for (size_t index = 0u; index < operations; ++index) {
        ais_state_snapshot_t *snapshot = NULL;
        ais_error_info_t error;
        size_t count = 0u;
        ais_error_info_init(&error);
        if (ais_ctx_state_snapshot(ctx, &snapshot, &error) != AIS_OK ||
            ais_state_snapshot_percept_count(snapshot, &count, &error) !=
                AIS_OK ||
            ais_state_snapshot_release(&snapshot, &error) != AIS_OK) {
            if (snapshot != NULL) {
                (void)ais_state_snapshot_release(&snapshot, &error);
            }
            return 0;
        }
    }
    return 1;
}

static int benchmark_mixed(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, size_t operations)
{
    for (size_t index = 0u; index < operations; ++index) {
        ais_error_info_t error;
        ais_ingest_result_t result;
        ais_state_summary_t summary;
        double value = 0.0;
        ais_ingest_result_init(&result);
        ais_state_summary_init(&summary);
        ais_error_info_init(&error);
        if (ais_publish_f64(ctx, sensor, (double)index, &result, &error) !=
                AIS_OK ||
            ais_latest_f64(ctx, sensor, &value, &error) != AIS_OK ||
            ais_ctx_refresh(ctx, &error) != AIS_OK ||
            ais_ctx_get_state_summary(ctx, &summary, &error) != AIS_OK) {
            return 0;
        }
    }
    return 1;
}

static int emit_benchmark(
    const char *name, ais_ctx_t *ctx, ais_sensor_id_t sensor,
    size_t operations, benchmark_operation_fn operation,
    aisthra_embedded_clock_fn clock_now)
{
    const double begin = clock_now();
    const int success = operation(ctx, sensor, operations);
    double elapsed_us = (clock_now() - begin) * 1000000.0;
    if (elapsed_us < 0.0) {
        elapsed_us = 0.0;
    }
    printf(
        "AISTHRA_EMBEDDED_BENCH workload=%s operations=%u elapsed_us=%.0f "
        "avg_us=%.3f throughput_ops_s=%.3f status=%s\n",
        name, (unsigned)operations, elapsed_us,
        operations == 0u ? 0.0 : elapsed_us / (double)operations,
        elapsed_us <= 0.0 ? 0.0 :
            ((double)operations * 1000000.0) / elapsed_us,
        success ? "PASS" : "FAIL");
    return success;
}

int aisthra_embedded_public_benchmark(aisthra_embedded_clock_fn clock_now)
{
    ais_ctx_t *ctx = NULL;
    ais_provider_config_t provider_config;
    ais_sensor_config_t sensor_config;
    ais_error_info_t error;
    ais_provider_id_t provider = 0u;
    ais_sensor_id_t sensor = 0u;
    int ok = 0;

    if (clock_now == NULL) {
        return 0;
    }
    ais_error_info_init(&error);
    ais_provider_config_init(&provider_config);
    ais_sensor_config_init(&sensor_config);
    if (benchmark_context_create(&ctx, &error) != AIS_OK) {
        return 0;
    }

    provider_config.key = AIS_SV("aisthra.embedded.benchmark.base-provider");
    provider_config.version = AIS_SV("1.0");
    if (ais_provider_register(
            ctx, &provider_config, &provider, &error) != AIS_OK ||
        ais_provider_start(ctx, provider, &error) != AIS_OK) {
        goto cleanup;
    }
    sensor_config.key = AIS_SV("aisthra.embedded.benchmark.base-sensor");
    sensor_config.schema = AIS_SV("environment.temperature.v1");
    sensor_config.unit = AIS_SV("celsius");
    sensor_config.reference_frame = AIS_SV("device");
    sensor_config.provider_id = provider;
    if (ais_sensor_register(ctx, &sensor_config, &sensor, &error) != AIS_OK ||
        !benchmark_publication(
            ctx, sensor, AISTHRA_EMBEDDED_PUBLICATION_OPERATIONS)) {
        goto cleanup;
    }

    ok = emit_benchmark(
             "context_create_close", ctx, sensor,
             AISTHRA_EMBEDDED_CONTEXT_OPERATIONS,
             benchmark_context_create_close, clock_now) &&
         emit_benchmark(
             "provider_register", ctx, sensor,
             AISTHRA_EMBEDDED_REGISTRATION_OPERATIONS,
             benchmark_provider_registration, clock_now) &&
         emit_benchmark(
             "sensor_register", ctx, sensor,
             AISTHRA_EMBEDDED_REGISTRATION_OPERATIONS,
             benchmark_sensor_registration, clock_now) &&
         emit_benchmark(
             "validation", ctx, sensor,
             AISTHRA_EMBEDDED_PUBLICATION_OPERATIONS,
             benchmark_validation, clock_now) &&
         emit_benchmark(
             "publication", ctx, sensor,
             AISTHRA_EMBEDDED_PUBLICATION_OPERATIONS,
             benchmark_publication, clock_now) &&
         emit_benchmark(
             "latest", ctx, sensor,
             AISTHRA_EMBEDDED_PUBLICATION_OPERATIONS,
             benchmark_latest, clock_now) &&
         emit_benchmark(
             "snapshot", ctx, sensor,
             AISTHRA_EMBEDDED_CONTEXT_OPERATIONS,
             benchmark_snapshot, clock_now) &&
         emit_benchmark(
             "refresh", ctx, sensor,
             AISTHRA_EMBEDDED_CONTEXT_OPERATIONS,
             benchmark_refresh, clock_now) &&
         emit_benchmark(
             "percept", ctx, sensor,
             AISTHRA_EMBEDDED_CONTEXT_OPERATIONS,
             benchmark_percept, clock_now) &&
         emit_benchmark(
             "mixed", ctx, sensor,
             AISTHRA_EMBEDDED_CONTEXT_OPERATIONS,
             benchmark_mixed, clock_now);

cleanup:
    if (sensor != 0u) {
        (void)ais_sensor_unregister(ctx, sensor, &error);
    }
    if (provider != 0u) {
        (void)ais_provider_unregister(ctx, provider, &error);
    }
    if (ctx != NULL) {
        (void)ais_ctx_close(&ctx, &error);
    }
    return ok;
}
