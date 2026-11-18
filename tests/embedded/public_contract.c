/* SPDX-License-Identifier: MIT */

#include "public_contract.h"

#include <aisthra/advanced.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct contract_provider_state {
    ais_sensor_id_t sensor;
    ais_provider_id_t provider;
    unsigned starts;
    unsigned stops;
    unsigned polls;
} contract_provider_state_t;

typedef struct contract_event_state {
    atomic_uint accepted;
    atomic_uint availability;
} contract_event_state_t;

static int require_status(
    ais_status_t actual, ais_status_t expected, const char *operation)
{
    if (actual != expected) {
        printf(
            "AISTHRA_CONTRACT:FAIL operation=%s expected=%s actual=%s\n",
            operation, ais_status_name(expected), ais_status_name(actual));
        return 0;
    }
    return 1;
}

static int require_condition(int condition, const char *operation)
{
    if (!condition) {
        printf("AISTHRA_CONTRACT:FAIL operation=%s\n", operation);
        return 0;
    }
    return 1;
}

static ais_status_t contract_context_create(
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

static ais_status_t contract_provider_start(void *user_data)
{
    contract_provider_state_t *state = (contract_provider_state_t *)user_data;
    if (state == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    state->starts += 1u;
    return AIS_OK;
}

static ais_status_t contract_provider_stop(void *user_data)
{
    contract_provider_state_t *state = (contract_provider_state_t *)user_data;
    if (state == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    state->stops += 1u;
    return AIS_OK;
}

static ais_status_t contract_provider_poll(
    void *user_data, ais_provider_emit_fn emit, void *emit_context)
{
    contract_provider_state_t *state = (contract_provider_state_t *)user_data;
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
    observation.provenance.source_type = AIS_SV("aisthra.embedded.contract");
    if (ais_payload_set_real(&observation.payload, 24.25) != AIS_OK) {
        return AIS_E_INTERNAL;
    }
    if (emit(emit_context, &observation, &result, &error) != AIS_OK) {
        return error.code == AIS_OK ? AIS_E_INTERNAL : error.code;
    }
    state->polls += 1u;
    return AIS_OK;
}

static void contract_event_callback(
    void *user_data, const ais_event_view_t *event)
{
    contract_event_state_t *state = (contract_event_state_t *)user_data;
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

int aisthra_embedded_public_contract(aisthra_embedded_parallel_fn parallel)
{
    ais_ctx_t *ctx = NULL;
    ais_provider_config_t provider_config;
    ais_provider_status_t provider_status;
    ais_sensor_config_t sensor_config;
    ais_sensor_status_t sensor_status;
    ais_state_summary_t summary;
    ais_state_snapshot_t *state_snapshot = NULL;
    ais_observation_snapshot_t *observation_snapshot = NULL;
    ais_observation_snapshot_t *snapshot_observation = NULL;
    ais_observation_view_t observation;
    ais_ingest_result_t result;
    ais_error_info_t error;
    contract_provider_state_t provider_state = {0};
    contract_event_state_t event_state;
    ais_provider_id_t provider = 0u;
    ais_sensor_id_t sensor = 0u;
    uint64_t subscription = 0u;
    size_t observation_count = 0u;
    size_t percept_count = 0u;
    uint8_t feature_supported = 0u;
    double latest = 0.0;
    int ok = 0;

    atomic_init(&event_state.accepted, 0u);
    atomic_init(&event_state.availability, 0u);
    ais_error_info_init(&error);
    ais_provider_config_init(&provider_config);
    ais_provider_status_init(&provider_status);
    ais_sensor_config_init(&sensor_config);
    ais_sensor_status_init(&sensor_status);
    ais_state_summary_init(&summary);
    ais_observation_view_init(&observation);
    ais_ingest_result_init(&result);

    if (!require_condition(parallel != NULL, "parallel-hook") ||
        !require_status(
            contract_context_create(&ctx, &error), AIS_OK, "ctx-create") ||
        !require_condition(ctx != NULL, "ctx-created")) {
        goto cleanup;
    }

    provider_config.key = AIS_SV("aisthra.embedded.contract.provider");
    provider_config.version = AIS_SV("1.0");
    provider_config.vtable.start = contract_provider_start;
    provider_config.vtable.stop = contract_provider_stop;
    provider_config.vtable.poll = contract_provider_poll;
    provider_config.user_data = &provider_state;
    if (!require_status(
            ais_provider_register(
                ctx, &provider_config, &provider, &error),
            AIS_OK, "provider-register")) {
        goto cleanup;
    }
    provider_state.provider = provider;

    sensor_config.key = AIS_SV("embedded-contract-temperature");
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

    if (!require_status(
            ais_event_subscribe(
                ctx, AIS_EVENT_ACCEPTED | AIS_EVENT_PROVIDER_AVAILABILITY,
                contract_event_callback, &event_state, NULL,
                &subscription, &error),
            AIS_OK, "event-subscribe") ||
        !require_status(
            ais_provider_start(ctx, provider, &error), AIS_OK,
            "provider-start") ||
        !require_condition(provider_state.starts == 1u, "provider-started")) {
        goto cleanup;
    }

    if (!require_status(
            ais_publish_f64(ctx, sensor, 23.5, &result, &error), AIS_OK,
            "publish-real") ||
        !require_condition(
            result.disposition == AIS_INGEST_ACCEPTED &&
                result.sequence == 1u,
            "publish-result") ||
        !require_status(
            ais_latest_f64(ctx, sensor, &latest, &error), AIS_OK,
            "latest-real") ||
        !require_condition(latest == 23.5, "latest-value") ||
        !require_status(
            ais_latest_f64(ctx, UINT64_C(0xffff), &latest, &error),
            AIS_E_UNKNOWN_SENSOR, "unknown-sensor")) {
        goto cleanup;
    }

    observation.sensor_id = sensor;
    if (!require_status(
            ais_observation_validate(ctx, &observation, &error),
            AIS_E_INVALID_OBSERVATION, "invalid-observation") ||
        !require_status(
            ais_sensor_latest_observation(
                ctx, sensor, &observation_snapshot, &error),
            AIS_OK, "observation-snapshot") ||
        !require_status(
            ais_observation_snapshot_get_view(
                observation_snapshot, &observation, &error),
            AIS_OK, "observation-view") ||
        !require_condition(
            observation.payload.kind == AIS_PAYLOAD_REAL,
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
            ais_provider_poll(ctx, provider, &error), AIS_OK,
            "provider-poll") ||
        !require_condition(provider_state.polls == 1u, "provider-emission") ||
        !require_status(
            ais_provider_get_status(
                ctx, provider, &provider_status, &error),
            AIS_OK, "provider-status") ||
        !require_condition(provider_status.observation_count >= 1u,
                           "provider-count") ||
        !require_condition(
            parallel(ctx, sensor) == 0, "concurrent-publication")) {
        goto cleanup;
    }

    if (!require_status(
            ais_feature_supported(0u, &feature_supported), AIS_OK,
            "feature-query") ||
        !require_status(
            ais_provider_set_availability(
                ctx, provider, AIS_AVAIL_DEGRADED,
                AIS_E_PROVIDER_UNAVAILABLE, &error),
            AIS_OK, "provider-degraded") ||
        !require_status(
            ais_publish_f64(ctx, sensor, 42.0, &result, &error),
            AIS_E_PROVIDER_UNAVAILABLE, "publish-unavailable") ||
        !require_status(
            ais_provider_set_availability(
                ctx, provider, AIS_AVAIL_ACTIVE, AIS_OK, &error),
            AIS_OK, "provider-active")) {
        goto cleanup;
    }

    if (!require_status(
            ais_provider_stop(ctx, provider, &error), AIS_OK,
            "provider-stop") ||
        !require_condition(provider_state.stops == 1u, "provider-stopped") ||
        !require_status(
            ais_sensor_get_status(ctx, sensor, &sensor_status, &error),
            AIS_OK, "sensor-status") ||
        !require_status(
            ais_sensor_unregister(ctx, sensor, &error), AIS_OK,
            "sensor-unregister") ||
        !require_status(
            ais_provider_unregister(ctx, provider, &error), AIS_OK,
            "provider-unregister") ||
        !require_status(
            ais_event_unsubscribe(ctx, subscription, &error), AIS_OK,
            "event-unsubscribe") ||
        !require_condition(
            atomic_load_explicit(&event_state.accepted, memory_order_relaxed) >
                0u,
            "accepted-event") ||
        !require_condition(
            atomic_load_explicit(
                &event_state.availability, memory_order_relaxed) > 0u,
            "availability-event")) {
        goto cleanup;
    }

    provider = 0u;
    sensor = 0u;
    subscription = 0u;
    if (!require_status(ais_ctx_close(&ctx, &error), AIS_OK, "ctx-close") ||
        !require_condition(ctx == NULL, "ctx-closed") ||
        !require_status(
            contract_context_create(&ctx, &error), AIS_OK, "ctx-recreate") ||
        !require_status(ais_ctx_close(&ctx, &error), AIS_OK,
                        "ctx-recreate-close") ||
        !require_condition(ctx == NULL, "ctx-recreated")) {
        goto cleanup;
    }

    ok = 1;

cleanup:
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
    if (ok) {
        printf("AISTHRA_CONTRACT:PASS\n");
    }
    return ok;
}
