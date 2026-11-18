/* SPDX-License-Identifier: MIT */

#include <aisthra/advanced.h>

#include <stdio.h>
#include <string.h>

typedef struct callback_state {
    int schema_calls;
    int schema_destroy_calls;
    int event_calls;
    int accepted_events;
    int availability_events;
} callback_state_t;

typedef struct provider_state {
    ais_ctx_t *ctx;
    ais_sensor_id_t sensor;
    ais_provider_id_t provider;
    int starts;
    int stops;
    int polls;
    int destroys;
} provider_state_t;

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

static ais_status_t custom_validate(
    void *user_data, const ais_observation_view_t *observation,
    ais_error_info_t *error)
{
    callback_state_t *state = (callback_state_t *)user_data;
    (void)error;
    if (state == NULL || observation == NULL ||
        observation->payload.kind != AIS_PAYLOAD_REAL) {
        return AIS_E_INVALID_OBSERVATION;
    }
    state->schema_calls += 1;
    return AIS_OK;
}

static void custom_destroy(void *user_data)
{
    callback_state_t *state = (callback_state_t *)user_data;
    if (state != NULL) {
        state->schema_destroy_calls += 1;
    }
}

static void event_callback(void *user_data, const ais_event_view_t *event)
{
    callback_state_t *state = (callback_state_t *)user_data;
    if (state == NULL || event == NULL) {
        return;
    }
    state->event_calls += 1;
    if (event->type == AIS_EVENT_ACCEPTED) {
        state->accepted_events += 1;
    }
    if (event->type == AIS_EVENT_PROVIDER_AVAILABILITY) {
        state->availability_events += 1;
    }
}

static ais_status_t provider_start(void *user_data)
{
    provider_state_t *state = (provider_state_t *)user_data;
    state->starts += 1;
    return AIS_OK;
}

static ais_status_t provider_stop(void *user_data)
{
    provider_state_t *state = (provider_state_t *)user_data;
    state->stops += 1;
    return AIS_OK;
}

static ais_status_t provider_poll(
    void *user_data, ais_provider_emit_fn emit, void *emit_context)
{
    provider_state_t *state = (provider_state_t *)user_data;
    ais_observation_view_t observation;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_status_t status;
    ais_observation_view_init(&observation);
    ais_ingest_result_init(&result);
    ais_error_info_init(&error);
    observation.sensor_id = state->sensor;
    observation.provider_id = state->provider;
    observation.acquisition = AIS_ACQUISITION_POINT;
    observation.observation_type = AIS_SV("environment.temperature.v1");
    observation.unit_code = AIS_UNIT_CELSIUS;
    observation.reference_frame_code = AIS_REF_DEVICE;
    if (ais_payload_set_real(&observation.payload, 24.25) != AIS_OK) {
        return AIS_E_INTERNAL;
    }
    observation.provenance.source_type = AIS_SV("test.provider");
    status = emit(emit_context, &observation, &result, &error);
    if (status == AIS_OK) {
        state->polls += 1;
    }
    return status;
}

static void provider_destroy(void *user_data)
{
    provider_state_t *state = (provider_state_t *)user_data;
    if (state != NULL) {
        state->destroys += 1;
    }
}

static int test_custom_schema(void)
{
    ais_ctx_t *ctx = NULL;
    ais_schema_config_t schema;
    ais_sensor_config_t sensor_config;
    ais_sensor_id_t sensor = 0u;
    ais_ingest_result_t result;
    ais_error_info_t error;
    callback_state_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    ais_error_info_init(&error);
    if (!check_status(ais_ctx_create_default(&ctx, &error), AIS_OK,
                       "custom-create")) {
        return 0;
    }
    ais_schema_config_init(&schema);
    schema.schema_id = AIS_SV("test.custom.real.v1");
    schema.modality = AIS_MODALITY_COMPUTATIONAL;
    schema.allowed_payload_kinds = AIS_PAYLOAD_MASK(AIS_PAYLOAD_REAL);
    schema.default_unit = AIS_SV("custom.unit");
    schema.default_reference_frame = AIS_SV("custom.frame");
    schema.callbacks.validate = custom_validate;
    schema.callbacks.destroy = custom_destroy;
    schema.user_data = &callbacks;
    if (!check_status(
            ais_schema_register(ctx, &schema, &error), AIS_OK,
            "custom-register")) {
        return 0;
    }
    ais_ingest_result_init(&result);
    ais_sensor_config_init(&sensor_config);
    sensor_config.key = AIS_SV("custom-sensor");
    sensor_config.schema = schema.schema_id;
    sensor_config.unit = schema.default_unit;
    sensor_config.reference_frame = schema.default_reference_frame;
    if (!check_status(
            ais_sensor_register(ctx, &sensor_config, &sensor, &error),
            AIS_OK, "custom-sensor-register") ||
        !check_status(
            ais_publish_f64(ctx, sensor, 4.0, &result, &error), AIS_OK,
            "custom-publish") || callbacks.schema_calls != 1) {
        return 0;
    }
    if (!check_status(
            ais_schema_unregister(ctx, schema.schema_id, &error),
            AIS_E_CONFLICT, "custom-schema-in-use")) {
        return 0;
    }
    if (!check_status(ais_sensor_unregister(ctx, sensor, &error), AIS_OK,
                      "custom-sensor-unregister") ||
        !check_status(
            ais_schema_unregister(ctx, schema.schema_id, &error), AIS_OK,
            "custom-schema-unregister") ||
        callbacks.schema_destroy_calls != 1) {
        return 0;
    }
    (void)ais_ctx_close(&ctx, &error);
    return ctx == NULL;
}

static int test_provider_and_events(void)
{
    ais_ctx_t *ctx = NULL;
    ais_provider_config_t provider_config;
    ais_sensor_config_t sensor_config;
    ais_provider_status_t provider_status;
    ais_provider_id_t provider = 0u;
    ais_sensor_id_t sensor = 0u;
    uint64_t subscription = 0u;
    provider_state_t provider_state;
    callback_state_t events;
    ais_error_info_t error;
    memset(&provider_state, 0, sizeof(provider_state));
    memset(&events, 0, sizeof(events));
    ais_error_info_init(&error);
    if (!check_status(ais_ctx_create_default(&ctx, &error), AIS_OK,
                      "provider-create")) {
        return 0;
    }
    if (!check_status(
            ais_event_subscribe(
                ctx, AIS_EVENT_ACCEPTED | AIS_EVENT_PROVIDER_AVAILABILITY,
                event_callback, &events, NULL, &subscription, &error),
            AIS_OK, "event-subscribe")) {
        return 0;
    }
    provider_state.ctx = ctx;
    ais_provider_config_init(&provider_config);
    provider_config.key = AIS_SV("test.provider");
    provider_config.version = AIS_SV("1.0");
    provider_config.vtable.start = provider_start;
    provider_config.vtable.stop = provider_stop;
    provider_config.vtable.poll = provider_poll;
    provider_config.vtable.destroy = provider_destroy;
    provider_config.user_data = &provider_state;
    if (!check_status(
            ais_provider_register(ctx, &provider_config, &provider, &error),
            AIS_OK, "provider-register")) {
        return 0;
    }
    provider_state.provider = provider;
    ais_sensor_config_init(&sensor_config);
    sensor_config.key = AIS_SV("provider-temperature");
    sensor_config.schema = AIS_SV("environment.temperature.v1");
    sensor_config.provider_id = provider;
    if (!check_status(
            ais_sensor_register(ctx, &sensor_config, &sensor, &error),
            AIS_OK, "provider-sensor-register")) {
        return 0;
    }
    provider_state.sensor = sensor;
    if (!check_status(
            ais_provider_start(ctx, provider, &error), AIS_OK,
            "provider-start") || provider_state.starts != 1 ||
        !check_status(
            ais_provider_poll(ctx, provider, &error), AIS_OK,
            "provider-poll") || provider_state.polls != 1 ||
        events.accepted_events != 1) {
        return 0;
    }
    ais_provider_status_init(&provider_status);
    if (!check_status(
            ais_provider_get_status(ctx, provider, &provider_status, &error),
            AIS_OK, "provider-status") ||
        provider_status.observation_count != 1u) {
        return 0;
    }
    if (!check_status(
            ais_provider_set_availability(
                ctx, provider, AIS_AVAIL_DEGRADED,
                AIS_E_PROVIDER_UNAVAILABLE, &error), AIS_OK,
            "provider-degraded") || events.availability_events != 2 ||
        !check_status(
            ais_provider_stop(ctx, provider, &error), AIS_OK,
            "provider-stop") || provider_state.stops != 1 ||
        events.availability_events != 3 ||
        !check_status(ais_sensor_unregister(ctx, sensor, &error), AIS_OK,
                      "provider-sensor-unregister") ||
        !check_status(
            ais_provider_unregister(ctx, provider, &error), AIS_OK,
            "provider-unregister") || provider_state.destroys != 1) {
        return 0;
    }
    if (!check_status(
            ais_event_unsubscribe(ctx, subscription, &error), AIS_OK,
            "event-unsubscribe")) {
        return 0;
    }
    (void)ais_ctx_close(&ctx, &error);
    return ctx == NULL;
}

static int test_manual_processing(void)
{
    ais_ctx_t *ctx = NULL;
    ais_ctx_config_t config;
    ais_sensor_id_t sensor = 0u;
    ais_ingest_result_t result;
    ais_state_summary_t summary;
    ais_state_snapshot_t *snapshot = NULL;
    ais_error_info_t error;
    size_t processed = 0u;
    size_t observations = 0u;
    ais_ctx_config_init(&config);
    config.processing_mode = AIS_PROCESS_MANUAL;
    config.max_recent_observations = 2u;
    ais_error_info_init(&error);
    if (!check_status(ais_ctx_create(&config, &ctx, &error), AIS_OK,
                      "manual-create") ||
        !check_status(
            ais_sensor_register_simple(
                ctx, AIS_SV("manual-temp"),
                AIS_SV("environment.temperature.v1"), AIS_SV("celsius"),
                &sensor, &error), AIS_OK, "manual-register")) {
        return 0;
    }
    ais_ingest_result_init(&result);
    if (!check_status(ais_publish_f64(ctx, sensor, 10.0, &result, &error),
                      AIS_OK, "manual-publish-1") ||
        result.disposition != AIS_INGEST_QUEUED ||
        !check_status(ais_publish_f64(ctx, sensor, 11.0, &result, &error),
                      AIS_OK, "manual-publish-2") ||
        !check_status(ais_publish_f64(ctx, sensor, 12.0, &result, &error),
                      AIS_E_BUFFER_TOO_SMALL, "manual-overflow")) {
        return 0;
    }
    ais_state_summary_init(&summary);
    if (!check_status(
            ais_ctx_get_state_summary(ctx, &summary, &error), AIS_OK,
            "manual-summary-before") || summary.processing_pending == 0u ||
        summary.recent_observation_count != 0u ||
        !check_status(ais_process(ctx, 1u, &processed, &error), AIS_OK,
                      "manual-process-one") || processed != 1u) {
        return 0;
    }
    if (!check_status(ais_process(ctx, 0u, &processed, &error), AIS_OK,
                      "manual-process-rest") || processed != 1u ||
        !check_status(
            ais_ctx_state_snapshot(ctx, &snapshot, &error), AIS_OK,
            "manual-snapshot") ||
        !check_status(
            ais_state_snapshot_observation_count(
                snapshot, &observations, &error), AIS_OK,
            "manual-snapshot-count") || observations != 2u) {
        return 0;
    }
    (void)ais_state_snapshot_release(&snapshot, &error);
    (void)ais_ctx_close(&ctx, &error);
    return ctx == NULL;
}

static int test_perceptive_schemas(void)
{
    ais_ctx_t *ctx = NULL;
    ais_sensor_id_t button = 0u;
    ais_sensor_id_t pointer = 0u;
    ais_sensor_id_t window = 0u;
    ais_sensor_id_t workspace = 0u;
    ais_sensor_id_t output = 0u;
    ais_ingest_result_t result;
    ais_observation_view_t observation;
    ais_state_snapshot_t *state_snapshot = NULL;
    ais_percept_snapshot_t *percept_snapshot = NULL;
    ais_percept_view_t percept;
    ais_sensor_config_t config;
    ais_error_info_t error;
    size_t percept_count = 0u;
    const char *expected_types[] = {
        "percept.local_interaction.v1",
        "percept.local_interaction.v1",
        "percept.desktop_window_observed.v1",
        "percept.desktop_workspace_observed.v1",
        "percept.desktop_output_observed.v1"
    };

    ais_error_info_init(&error);
    if (!check_status(ais_ctx_create_default(&ctx, &error), AIS_OK,
                      "schemas-create")) {
        return 0;
    }
    ais_sensor_config_init(&config);
    config.key = AIS_SV("button");
    config.schema = AIS_SV("interaction.button.v1");
    if (!check_status(ais_sensor_register(ctx, &config, &button, &error),
                      AIS_OK, "button-register")) {
        return 0;
    }
    config.key = AIS_SV("pointer");
    config.schema = AIS_SV("interaction.pointer_press.v1");
    if (!check_status(ais_sensor_register(ctx, &config, &pointer, &error),
                      AIS_OK, "pointer-register")) {
        return 0;
    }
    config.key = AIS_SV("window");
    config.schema = AIS_SV("desktop.window_geometry.v1");
    if (!check_status(ais_sensor_register(ctx, &config, &window, &error),
                      AIS_OK, "window-register")) {
        return 0;
    }
    config.key = AIS_SV("workspace");
    config.schema = AIS_SV("desktop.workspace_snapshot.v1");
    if (!check_status(ais_sensor_register(ctx, &config, &workspace, &error),
                      AIS_OK, "workspace-register")) {
        return 0;
    }
    config.key = AIS_SV("output");
    config.schema = AIS_SV("desktop.output_geometry.v1");
    if (!check_status(ais_sensor_register(ctx, &config, &output, &error),
                      AIS_OK, "output-register")) {
        return 0;
    }

    ais_ingest_result_init(&result);
    ais_observation_view_init(&observation);
    observation.sensor_id = button;
    if (ais_payload_set_event(
            &observation.payload, AIS_SV("pressed"), AIS_SV("left")) !=
            AIS_OK ||
        !check_status(
            ais_publish_observation(ctx, &observation, &result, &error),
            AIS_OK, "button-publish")) {
        return 0;
    }
    ais_observation_view_init(&observation);
    observation.sensor_id = pointer;
    if (ais_payload_set_structure(
            &observation.payload, AIS_SV("pointer-event"), 10.0, 20.0,
            0.0, 0.0, AIS_SV("motion")) != AIS_OK ||
        !check_status(
            ais_publish_observation(ctx, &observation, &result, &error),
            AIS_OK, "pointer-publish")) {
        return 0;
    }
    ais_observation_view_init(&observation);
    observation.sensor_id = window;
    if (ais_payload_set_structure(
            &observation.payload, AIS_SV("window"), 1.0, 2.0, 800.0,
            600.0, AIS_SV("output-1")) != AIS_OK ||
        !check_status(
            ais_publish_observation(ctx, &observation, &result, &error),
            AIS_OK, "window-publish")) {
        return 0;
    }
    ais_observation_view_init(&observation);
    observation.sensor_id = workspace;
    if (ais_payload_set_structure(
            &observation.payload, AIS_SV("workspace"), 0.0, 0.0, 0.0,
            0.0, AIS_SV("output-1")) != AIS_OK ||
        !check_status(
            ais_publish_observation(ctx, &observation, &result, &error),
            AIS_OK, "workspace-publish")) {
        return 0;
    }
    ais_observation_view_init(&observation);
    observation.sensor_id = output;
    if (ais_payload_set_structure(
            &observation.payload, AIS_SV("output"), 0.0, 0.0, 1920.0,
            1080.0, AIS_SV("output-1")) != AIS_OK ||
        !check_status(
            ais_publish_observation(ctx, &observation, &result, &error),
            AIS_OK, "output-publish")) {
        return 0;
    }
    if (!check_status(
            ais_ctx_state_snapshot(ctx, &state_snapshot, &error), AIS_OK,
            "schemas-snapshot") ||
        !check_status(
            ais_state_snapshot_percept_count(
                state_snapshot, &percept_count, &error), AIS_OK,
            "schemas-percept-count") || percept_count != 5u) {
        return 0;
    }
    for (size_t index = 0u; index < percept_count; ++index) {
        if (!check_status(
                ais_state_snapshot_percept_at(
                    state_snapshot, index, &percept_snapshot, &error),
                AIS_OK, "schemas-percept-at")) {
            return 0;
        }
        ais_percept_view_init(&percept);
        if (!check_status(
                ais_percept_snapshot_get_view(
                    percept_snapshot, &percept, &error), AIS_OK,
                "schemas-percept-view") ||
            percept.type.size != strlen(expected_types[index]) ||
            memcmp(percept.type.data, expected_types[index], percept.type.size) !=
                0 || percept.validity != AIS_VALIDITY_INFERRED) {
            return 0;
        }
        (void)ais_percept_snapshot_release(&percept_snapshot, &error);
    }
    (void)ais_state_snapshot_release(&state_snapshot, &error);

    ais_observation_view_init(&observation);
    observation.sensor_id = button;
    if (ais_payload_set_real(&observation.payload, 1.0) != AIS_OK ||
        !check_status(
            ais_publish_observation(ctx, &observation, &result, &error),
            AIS_E_INVALID_OBSERVATION, "button-invalid-payload")) {
        return 0;
    }
    (void)ais_ctx_close(&ctx, &error);
    return ctx == NULL;
}

int main(void)
{
    if (!test_custom_schema()) {
        return 1;
    }
    if (!test_provider_and_events()) {
        return 1;
    }
    if (!test_manual_processing()) {
        return 1;
    }
    if (!test_perceptive_schemas()) {
        return 1;
    }
    return 0;
}
