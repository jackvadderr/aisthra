/* SPDX-License-Identifier: MIT */

#include <aisthra/advanced.h>

#include "aisthra_test_hooks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static int expect_status(
    ais_status_t actual, ais_status_t expected, const char *operation)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %s, got %s\n", operation,
                ais_status_name(expected), ais_status_name(actual));
        return 0;
    }
    return 1;
}

typedef struct blocking_schema_state {
    mtx_t mutex;
    cnd_t condition;
    int entered;
    int release;
    int calls;
} blocking_schema_state_t;

static ais_status_t blocking_schema_validate(
    void *user_data, const ais_observation_view_t *observation,
    ais_error_info_t *error)
{
    blocking_schema_state_t *state = (blocking_schema_state_t *)user_data;
    (void)observation;
    (void)error;
    (void)mtx_lock(&state->mutex);
    state->entered = 1;
    state->calls += 1;
    (void)cnd_broadcast(&state->condition);
    while (!state->release) {
        if (cnd_wait(&state->condition, &state->mutex) != thrd_success) {
            (void)mtx_unlock(&state->mutex);
            return AIS_E_INTERNAL;
        }
    }
    (void)mtx_unlock(&state->mutex);
    return AIS_OK;
}

typedef struct publish_thread_state {
    ais_ctx_t *ctx;
    ais_sensor_id_t sensor;
    ais_status_t status;
} publish_thread_state_t;

static int publish_thread_main(void *argument)
{
    publish_thread_state_t *state = (publish_thread_state_t *)argument;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_ingest_result_init(&result);
    ais_error_info_init(&error);
    state->status = ais_publish_f64(
        state->ctx, state->sensor, 12.0, &result, &error);
    return 0;
}

static int test_schema_stability(void)
{
    ais_ctx_t *ctx = NULL;
    ais_schema_config_t config;
    ais_sensor_config_t sensor_config;
    ais_sensor_id_t sensor = 0u;
    blocking_schema_state_t state;
    publish_thread_state_t publisher;
    thrd_t thread;
    ais_error_info_t error;
    char schema_ids[24][64];
    ais_string_view_t registered_ids[24];
    size_t registered_count = 0u;
    int thread_result;

    memset(&state, 0, sizeof(state));
    if (mtx_init(&state.mutex, mtx_plain) != thrd_success ||
        cnd_init(&state.condition) != thrd_success) {
        return 0;
    }
    ais_error_info_init(&error);
    if (!expect_status(
            ais_ctx_create_default(&ctx, &error), AIS_OK, "schema-create")) {
        return 0;
    }
    ais_schema_config_init(&config);
    config.schema_id = AIS_SV("test.blocked.v1");
    config.modality = AIS_MODALITY_COMPUTATIONAL;
    config.allowed_payload_kinds = AIS_PAYLOAD_MASK(AIS_PAYLOAD_REAL);
    config.default_unit = AIS_SV("custom.unit");
    config.default_reference_frame = AIS_SV("custom.frame");
    config.callbacks.validate = blocking_schema_validate;
    config.user_data = &state;
    if (!expect_status(
            ais_schema_register(ctx, &config, &error), AIS_OK,
            "schema-register") ) {
        return 0;
    }
    ais_sensor_config_init(&sensor_config);
    sensor_config.key = AIS_SV("blocked-sensor");
    sensor_config.schema = config.schema_id;
    sensor_config.unit = config.default_unit;
    sensor_config.reference_frame = config.default_reference_frame;
    if (!expect_status(
            ais_sensor_register(ctx, &sensor_config, &sensor, &error),
            AIS_OK, "schema-sensor-register")) {
        return 0;
    }
    publisher.ctx = ctx;
    publisher.sensor = sensor;
    publisher.status = AIS_E_INTERNAL;
    if (thrd_create(&thread, publish_thread_main, &publisher) != thrd_success) {
        return 0;
    }
    (void)mtx_lock(&state.mutex);
    while (!state.entered) {
        if (cnd_wait(&state.condition, &state.mutex) != thrd_success) {
            (void)mtx_unlock(&state.mutex);
            return 0;
        }
    }
    (void)mtx_unlock(&state.mutex);

    for (size_t index = 0u; index < 24u; ++index) {
        int written = snprintf(
            schema_ids[index], sizeof(schema_ids[index]),
            "test.grow.%zu.v1", index);
        ais_schema_config_t growth;
        ais_schema_config_init(&growth);
        growth.schema_id = ais_string_view_make(
            schema_ids[index], (size_t)written);
        growth.modality = AIS_MODALITY_COMPUTATIONAL;
        growth.allowed_payload_kinds = AIS_PAYLOAD_MASK(AIS_PAYLOAD_REAL);
        growth.default_unit = config.default_unit;
        growth.default_reference_frame = config.default_reference_frame;
        if (!expect_status(
                ais_schema_register(ctx, &growth, &error), AIS_OK,
                "schema-growth-register")) {
            return 0;
        }
        registered_ids[registered_count++] = growth.schema_id;
    }
    if (!expect_status(
            ais_schema_unregister(ctx, registered_ids[0], &error), AIS_OK,
            "schema-growth-unregister")) {
        return 0;
    }
    (void)mtx_lock(&state.mutex);
    state.release = 1;
    (void)cnd_broadcast(&state.condition);
    (void)mtx_unlock(&state.mutex);
    if (thrd_join(thread, &thread_result) != thrd_success ||
        !expect_status(publisher.status, AIS_OK, "schema-blocked-publish") ||
        state.calls != 1) {
        return 0;
    }
    for (size_t index = 1u; index < registered_count; ++index) {
        if (!expect_status(
                ais_schema_unregister(ctx, registered_ids[index], &error),
                AIS_OK, "schema-growth-cleanup")) {
            return 0;
        }
    }
    if (!expect_status(
            ais_sensor_unregister(ctx, sensor, &error), AIS_OK,
            "schema-sensor-unregister") ||
        !expect_status(
            ais_schema_unregister(ctx, config.schema_id, &error), AIS_OK,
            "schema-unregister")) {
        return 0;
    }
    (void)ais_ctx_close(&ctx, &error);
    cnd_destroy(&state.condition);
    mtx_destroy(&state.mutex);
    return ctx == NULL;
}

typedef struct poll_state {
    ais_sensor_id_t sensor;
    ais_provider_id_t emitted_provider;
} poll_state_t;

static ais_status_t isolation_poll(
    void *user_data, ais_provider_emit_fn emit, void *emit_context)
{
    poll_state_t *state = (poll_state_t *)user_data;
    ais_observation_view_t observation;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_observation_view_init(&observation);
    ais_ingest_result_init(&result);
    ais_error_info_init(&error);
    observation.sensor_id = state->sensor;
    observation.provider_id = state->emitted_provider;
    (void)ais_payload_set_real(&observation.payload, 7.0);
    return emit(emit_context, &observation, &result, &error);
}

static int test_provider_isolation(void)
{
    ais_ctx_t *ctx = NULL;
    ais_provider_config_t provider_config;
    ais_sensor_config_t sensor_config;
    ais_provider_id_t provider_a = 0u;
    ais_provider_id_t provider_b = 0u;
    ais_sensor_id_t sensor_a = 0u;
    ais_sensor_id_t sensor_b = 0u;
    ais_provider_status_t status;
    poll_state_t poll_state;
    ais_error_info_t error;
    ais_ingest_result_t result;

    ais_error_info_init(&error);
    if (ais_ctx_create_default(&ctx, &error) != AIS_OK) return 0;
    ais_provider_config_init(&provider_config);
    provider_config.key = AIS_SV("provider-a");
    provider_config.vtable.poll = isolation_poll;
    poll_state.sensor = 0u;
    poll_state.emitted_provider = 0u;
    provider_config.user_data = &poll_state;
    if (ais_provider_register(ctx, &provider_config, &provider_a, &error) != AIS_OK) {
        return 0;
    }
    provider_config.key = AIS_SV("provider-b");
    provider_config.user_data = NULL;
    if (ais_provider_register(ctx, &provider_config, &provider_b, &error) != AIS_OK) {
        return 0;
    }
    ais_sensor_config_init(&sensor_config);
    sensor_config.key = AIS_SV("sensor-a");
    sensor_config.schema = AIS_SV("environment.temperature.v1");
    sensor_config.unit = AIS_SV("celsius");
    sensor_config.provider_id = provider_a;
    if (ais_sensor_register(ctx, &sensor_config, &sensor_a, &error) != AIS_OK) {
        return 0;
    }
    sensor_config.key = AIS_SV("sensor-b");
    sensor_config.provider_id = provider_b;
    if (ais_sensor_register(ctx, &sensor_config, &sensor_b, &error) != AIS_OK) {
        return 0;
    }
    poll_state.sensor = sensor_b;
    poll_state.emitted_provider = provider_b;
    if (ais_provider_start(ctx, provider_a, &error) != AIS_OK ||
        ais_provider_start(ctx, provider_b, &error) != AIS_OK ||
        ais_provider_poll(ctx, provider_a, &error) != AIS_E_INVALID_OBSERVATION) {
        return 0;
    }
    ais_provider_status_init(&status);
    if (ais_provider_get_status(ctx, provider_b, &status, &error) != AIS_OK ||
        status.observation_count != 0u) {
        return 0;
    }
    poll_state.sensor = sensor_a;
    poll_state.emitted_provider = 0u;
    ais_ingest_result_init(&result);
    if (ais_provider_poll(ctx, provider_a, &error) != AIS_OK ||
        ais_provider_get_status(ctx, provider_a, &status, &error) != AIS_OK ||
        status.observation_count != 1u) {
        return 0;
    }
    (void)ais_provider_stop(ctx, provider_a, &error);
    (void)ais_provider_stop(ctx, provider_b, &error);
    (void)ais_sensor_unregister(ctx, sensor_a, &error);
    (void)ais_sensor_unregister(ctx, sensor_b, &error);
    (void)ais_provider_unregister(ctx, provider_a, &error);
    (void)ais_provider_unregister(ctx, provider_b, &error);
    (void)ais_ctx_close(&ctx, &error);
    return ctx == NULL;
}

static int test_validation_and_masks(void)
{
    ais_ctx_t *ctx = NULL;
    ais_sensor_id_t sensor = 0u;
    ais_sensor_status_t sensor_status;
    ais_observation_view_t observation;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_schema_config_t schema;
    char long_id[4097];

    ais_error_info_init(&error);
    if (ais_ctx_create_default(&ctx, &error) != AIS_OK ||
        ais_sensor_register_simple(
            ctx, AIS_SV("validation"),
            AIS_SV("environment.temperature.v1"), AIS_SV("celsius"),
            &sensor, &error) != AIS_OK) {
        return 0;
    }
    ais_observation_view_init(&observation);
    observation.sensor_id = sensor;
    (void)ais_payload_set_real(&observation.payload, 3.0);
    if (ais_observation_validate(ctx, &observation, &error) != AIS_OK ||
        ais_observation_validate(ctx, &observation, &error) != AIS_OK) {
        return 0;
    }
    ais_sensor_status_init(&sensor_status);
    if (ais_sensor_get_status(ctx, sensor, &sensor_status, &error) != AIS_OK ||
        sensor_status.has_latest != 0u || sensor_status.observation_count != 0u) {
        return 0;
    }
    ais_ingest_result_init(&result);
    if (ais_publish_f64(ctx, sensor, 3.0, &result, &error) != AIS_OK ||
        result.sequence != 1u ||
        result.observation_id.size != strlen("ais-observation-1") ||
        memcmp(result.observation_id.data, "ais-observation-1", result.observation_id.size) != 0) {
        return 0;
    }
    ais_schema_config_init(&schema);
    schema.schema_id = AIS_SV("reserved.bytes");
    schema.modality = AIS_MODALITY_COMPUTATIONAL;
    schema.allowed_payload_kinds = AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES);
    if (ais_schema_register(ctx, &schema, &error) != AIS_E_INVALID_ARGUMENT) return 0;
    schema.schema_id = AIS_SV("reserved.mixed");
    schema.allowed_payload_kinds =
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_REAL) | AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES);
    if (ais_schema_register(ctx, &schema, &error) != AIS_E_INVALID_ARGUMENT) return 0;
    schema.schema_id = AIS_SV("reserved.unknown");
    schema.allowed_payload_kinds = UINT32_C(1) << 31u;
    if (ais_schema_register(ctx, &schema, &error) != AIS_E_INVALID_ARGUMENT) return 0;
    memset(long_id, 'x', sizeof(long_id) - 1u);
    long_id[sizeof(long_id) - 1u] = '\0';
    ais_observation_view_init(&observation);
    observation.sensor_id = sensor;
    observation.observation_id = ais_string_view_make(long_id, sizeof(long_id) - 1u);
    (void)ais_payload_set_real(&observation.payload, 4.0);
    ais_ingest_result_init(&result);
    if (ais_publish_observation(ctx, &observation, &result, &error) !=
            AIS_E_BUFFER_TOO_SMALL || result.disposition != AIS_INGEST_REJECTED) {
        return 0;
    }
    if (ais_publish_observation(ctx, &observation, NULL, &error) !=
        AIS_E_BUFFER_TOO_SMALL) {
        return 0;
    }
    (void)ais_ctx_close(&ctx, &error);
    return ctx == NULL;
}

static int test_fault_injection(void)
{
    ais_ctx_t *ctx = NULL;
    ais_sensor_id_t sensor = 0u;
    ais_sensor_id_t event_sensor = 0u;
    ais_sensor_config_t event_config;
    ais_observation_view_t event_observation;
    ais_ingest_result_t event_result;
    ais_observation_snapshot_t *observation = NULL;
    ais_state_snapshot_t *state = NULL;
    ais_error_info_t error;
    ais_ingest_result_t result;
    ais_status_t status;

    ais_error_info_init(&error);
    if (ais_ctx_create_default(&ctx, &error) != AIS_OK ||
        ais_sensor_register_simple(
            ctx, AIS_SV("faults"), AIS_SV("environment.temperature.v1"),
            AIS_SV("celsius"), &sensor, &error) != AIS_OK) {
        return 0;
    }
    ais_sensor_config_init(&event_config);
    event_config.key = AIS_SV("fault-events");
    event_config.schema = AIS_SV("interaction.button.v1");
    if (ais_sensor_register(ctx, &event_config, &event_sensor, &error) !=
        AIS_OK) {
        return 0;
    }
    ais_observation_view_init(&event_observation);
    event_observation.sensor_id = event_sensor;
    if (ais_payload_set_event(
            &event_observation.payload, AIS_SV("pressed"),
            AIS_SV("left")) != AIS_OK) {
        return 0;
    }
    ais_ingest_result_init(&event_result);
    for (long failure = 0; failure < 24; ++failure) {
        ais_test_alloc_fail_after(failure);
        (void)ais_publish_observation(
            ctx, &event_observation, &event_result, &error);
        ais_test_alloc_reset();
    }
    ais_ingest_result_init(&result);
    if (ais_publish_f64(ctx, sensor, 1.0, &result, &error) != AIS_OK) return 0;
    for (long failure = 0; failure < 24; ++failure) {
        ais_test_alloc_fail_after(failure);
        observation = NULL;
        status = ais_sensor_latest_observation(
            ctx, sensor, &observation, &error);
        ais_test_alloc_reset();
        if (status == AIS_OK && observation == NULL) return 0;
        if (observation != NULL &&
            ais_observation_snapshot_release(&observation, &error) != AIS_OK) {
            return 0;
        }
        state = NULL;
        ais_test_alloc_fail_after(failure);
        status = ais_ctx_state_snapshot(ctx, &state, &error);
        ais_test_alloc_reset();
        if (status == AIS_OK && state == NULL) return 0;
        if (state != NULL &&
            ais_state_snapshot_release(&state, &error) != AIS_OK) return 0;
    }
    (void)ais_ctx_close(&ctx, &error);
    return ctx == NULL;
}

static int test_sensor_scale(void)
{
    const size_t sizes[] = {0u, 1u, 10u, 100u, 1000u};
    ais_error_info_t error;
    ais_ingest_result_t result;
    ais_sensor_status_t sensor_status;
    ais_sensor_id_t *sensors = NULL;
    ais_ctx_t *ctx = NULL;
    ais_status_t status;
    char key[64];
    ais_error_info_init(&error);
    for (size_t case_index = 0u; case_index < sizeof(sizes) / sizeof(sizes[0]);
         ++case_index) {
        size_t count = sizes[case_index];
        if (ais_ctx_create_default(&ctx, &error) != AIS_OK) return 0;
        if (count != 0u) {
            sensors = (ais_sensor_id_t *)calloc(count, sizeof(*sensors));
            if (sensors == NULL) return 0;
        }
        for (size_t index = 0u; index < count; ++index) {
            int written = snprintf(key, sizeof(key), "scale-%zu", index);
            if (written < 0 || (size_t)written >= sizeof(key) ||
                ais_sensor_register_simple(
                    ctx, ais_string_view_make(key, (size_t)written),
                    AIS_SV("environment.temperature.v1"), AIS_SV("celsius"),
                    &sensors[index], &error) != AIS_OK) {
                free(sensors);
                return 0;
            }
            ais_ingest_result_init(&result);
            status = ais_publish_f64(ctx, sensors[index], (double)index,
                                     &result, &error);
            if (status != AIS_OK || result.sequence != 1u) {
                free(sensors);
                return 0;
            }
        }
        if (count != 0u) {
            ais_sensor_status_init(&sensor_status);
            if (ais_sensor_get_status(
                    ctx, sensors[count - 1u], &sensor_status, &error) != AIS_OK ||
                sensor_status.observation_count != 1u ||
                sensor_status.has_latest == 0u) {
                free(sensors);
                return 0;
            }
        }
        free(sensors);
        sensors = NULL;
        if (ais_ctx_close(&ctx, &error) != AIS_OK || ctx != NULL) return 0;
    }
    return 1;
}

static int test_sequence_limits(void)
{
    ais_ctx_t *ctx = NULL;
    ais_sensor_id_t sensor = 0u;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_observation_view_t explicit_observation;
    ais_ingest_result_init(&result);
    ais_error_info_init(&error);
    if (ais_ctx_create_default(&ctx, &error) != AIS_OK ||
        ais_sensor_register_simple(
            ctx, AIS_SV("sequence-limits"),
            AIS_SV("environment.temperature.v1"), AIS_SV("celsius"),
            &sensor, &error) != AIS_OK) {
        return 0;
    }
    ais_test_set_sensor_sequence(ctx, sensor, UINT64_MAX - 2u);
    if (ais_publish_f64(ctx, sensor, 1.0, &result, &error) != AIS_OK ||
        result.sequence != UINT64_MAX - 2u ||
        ais_publish_f64(ctx, sensor, 2.0, &result, &error) != AIS_OK ||
        result.sequence != UINT64_MAX - 1u ||
        ais_publish_f64(ctx, sensor, 3.0, &result, &error) != AIS_OK ||
        result.sequence != UINT64_MAX ||
        ais_publish_f64(ctx, sensor, 4.0, &result, &error) !=
            AIS_E_BUFFER_TOO_SMALL) {
        return 0;
    }
    ais_observation_view_init(&explicit_observation);
    explicit_observation.sensor_id = sensor;
    explicit_observation.sequence = UINT64_MAX;
    (void)ais_payload_set_real(&explicit_observation.payload, 5.0);
    if (ais_publish_observation(
            ctx, &explicit_observation, &result, &error) != AIS_E_DUPLICATE) {
        return 0;
    }
    if (ais_ctx_close(&ctx, &error) != AIS_OK || ctx != NULL) return 0;
    return 1;
}

int main(void)
{
    if (!test_schema_stability() ||
        !test_provider_isolation() ||
        !test_validation_and_masks() ||
        !test_fault_injection() ||
        !test_sensor_scale() ||
        !test_sequence_limits()) {
        return 1;
    }
    return 0;
}
