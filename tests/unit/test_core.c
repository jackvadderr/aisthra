/* SPDX-License-Identifier: MIT */

#include <aisthra/advanced.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int check_status(
    ais_status_t actual, ais_status_t expected, const char *operation)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %s, got %s\n", operation,
                ais_status_name(expected), ais_status_name(actual));
        return 0;
    }
    return 1;
}

static int check_text(ais_string_view_t value, const char *expected)
{
    size_t size = strlen(expected);
    return value.size == size && value.data != NULL &&
        memcmp(value.data, expected, size) == 0;
}

int main(void)
{
    ais_ctx_t *ctx = NULL;
    ais_ctx_t *stale_ctx = NULL;
    ais_ctx_config_t config;
    ais_ctx_config_t stale_config;
    ais_sensor_config_t sensor_config;
    ais_sensor_id_t sensor = 0u;
    ais_sensor_id_t duplicate = 0u;
    ais_sensor_status_t sensor_status;
    ais_state_summary_t summary;
    ais_observation_snapshot_t *observation_snapshot = NULL;
    ais_observation_view_t observation_view;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_status_t status;
    double value = 0.0;
    int64_t integer_value = 0;

    ais_error_info_init(&error);
    ais_ctx_config_init(&config);
    if (!check_status(ais_ctx_create(&config, &ctx, &error), AIS_OK,
                      "create") || ctx == NULL) {
        return 1;
    }
    if (!check_status(ais_ctx_close(NULL, &error), AIS_E_INVALID_ARGUMENT,
                      "close-null-address")) {
        return 1;
    }

    ais_sensor_config_init(&sensor_config);
    sensor_config.key = AIS_SV("temperature-0");
    sensor_config.schema = AIS_SV("environment.temperature.v1");
    sensor_config.unit = AIS_SV("celsius");
    if (!check_status(
            ais_sensor_register(ctx, &sensor_config, &sensor, &error),
            AIS_OK, "register") || sensor == 0u) {
        return 1;
    }
    if (!check_status(
            ais_sensor_register(ctx, &sensor_config, &duplicate, &error),
            AIS_E_CONFLICT, "duplicate-register")) {
        return 1;
    }
    if (!check_status(
            ais_sensor_register_simple(
                ctx, AIS_SV("bad"), AIS_SV("missing.v1"), AIS_SV(""),
                &duplicate, &error),
            AIS_E_SCHEMA_MISMATCH, "unknown-schema")) {
        return 1;
    }
    ais_sensor_status_init(&sensor_status);
    if (!check_status(
            ais_sensor_get_status(ctx, sensor, &sensor_status, &error),
            AIS_OK, "status-before-publish") || sensor_status.has_latest != 0u) {
        return 1;
    }
    ais_ingest_result_init(&result);
    status = ais_publish_f64(ctx, sensor, 21.5, &result, &error);
    if (!check_status(status, AIS_OK, "publish-real") ||
        result.disposition != AIS_INGEST_ACCEPTED || result.sequence != 1u) {
        return 1;
    }
    if (!check_status(
            ais_latest_f64(ctx, sensor, &value, &error), AIS_OK,
            "latest-real") || fabs(value - 21.5) > 0.000001) {
        return 1;
    }
    if (!check_status(
            ais_latest_i64(ctx, sensor, &integer_value, &error),
            AIS_E_TYPE_MISMATCH, "latest-type-mismatch")) {
        return 1;
    }

    ais_state_summary_init(&summary);
    if (!check_status(
            ais_ctx_get_state_summary(ctx, &summary, &error), AIS_OK,
            "summary") || summary.recent_observation_count != 1u ||
        summary.rejected_observation_count != 0u) {
        return 1;
    }
    if (!check_status(
            ais_sensor_latest_observation(
                ctx, sensor, &observation_snapshot, &error), AIS_OK,
            "latest-snapshot")) {
        return 1;
    }
    ais_observation_view_init(&observation_view);
    if (!check_status(
            ais_observation_snapshot_get_view(
                observation_snapshot, &observation_view, &error), AIS_OK,
            "snapshot-view") ||
        !check_text(observation_view.observation_type,
                    "environment.temperature.v1") ||
        observation_view.payload.kind != AIS_PAYLOAD_REAL) {
        return 1;
    }
    (void)ais_observation_snapshot_release(&observation_snapshot, &error);

    if (!check_status(
            ais_provider_set_availability(
                ctx, 1u, AIS_AVAIL_STOPPED, AIS_E_PROVIDER_UNAVAILABLE,
                &error), AIS_OK, "provider-stop") ||
        !check_status(
            ais_publish_f64(ctx, sensor, 22.0, &result, &error),
            AIS_E_PROVIDER_UNAVAILABLE, "publish-unavailable") ||
        !check_status(
            ais_provider_set_availability(
                ctx, 1u, AIS_AVAIL_ACTIVE, AIS_OK, &error),
            AIS_OK, "provider-restart")) {
        return 1;
    }

    ais_ctx_config_init(&stale_config);
    stale_config.flags = AIS_CTX_ALLOW_EXTERNAL_RECEIPT;
    stale_config.stale_after_ns = 1u;
    if (!check_status(
            ais_ctx_create(&stale_config, &stale_ctx, &error), AIS_OK,
            "create-stale") ||
        !check_status(
            ais_sensor_register_simple(
                stale_ctx, AIS_SV("stale"),
                AIS_SV("environment.temperature.v1"), AIS_SV("celsius"),
                &duplicate, &error), AIS_OK, "register-stale")) {
        return 1;
    }
    ais_observation_view_init(&observation_view);
    observation_view.sensor_id = duplicate;
    observation_view.payload.kind = AIS_PAYLOAD_REAL;
    observation_view.payload.as.real_value = 18.0;
    if (ais_timestamp_set(
            &observation_view.received_at, 0.0,
            AIS_CLOCK_PROCESS_MONOTONIC,
            AIS_SV("aisthra.process.monotonic"), 1u) != AIS_OK ||
        !check_status(
            ais_publish_observation(
                stale_ctx, &observation_view, &result, &error), AIS_OK,
            "publish-stale")) {
        return 1;
    }
    if (!check_status(
            ais_latest_f64(stale_ctx, duplicate, &value, &error),
            AIS_E_STALE, "latest-stale")) {
        return 1;
    }
    if (!check_status(ais_ctx_refresh(stale_ctx, &error), AIS_OK,
                      "refresh-stale")) {
        return 1;
    }
    if (!check_status(ais_ctx_close(&stale_ctx, &error), AIS_OK,
                      "close-stale") || stale_ctx != NULL) {
        return 1;
    }

    ais_ctx_config_init(&config);
    config.processing_mode = 99u;
    if (!check_status(ais_ctx_create(&config, &stale_ctx, &error),
                      AIS_E_INVALID_ARGUMENT, "invalid-config") ||
        stale_ctx != NULL) {
        return 1;
    }
    if (!check_status(ais_sensor_unregister(ctx, sensor, &error), AIS_OK,
                      "unregister") ||
        !check_status(
            ais_latest_f64(ctx, sensor, &value, &error),
            AIS_E_UNKNOWN_SENSOR, "latest-unregistered")) {
        return 1;
    }
    if (!check_status(ais_ctx_close(&ctx, &error), AIS_OK, "close") ||
        ctx != NULL ||
        !check_status(ais_ctx_close(&ctx, &error), AIS_OK, "close-repeat")) {
        return 1;
    }
    return 0;
}
