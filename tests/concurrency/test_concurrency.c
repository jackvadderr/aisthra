/* SPDX-License-Identifier: MIT */

#include <aisthra/advanced.h>

#include <stdio.h>
#include <string.h>
#include <threads.h>

typedef struct publish_worker {
    ais_ctx_t *ctx;
    ais_sensor_id_t sensor;
    int count;
    int failures;
} publish_worker_t;

typedef struct duplicate_worker {
    ais_ctx_t *ctx;
    ais_sensor_id_t sensor;
    int result;
} duplicate_worker_t;

static int publish_worker_main(void *argument)
{
    publish_worker_t *worker = (publish_worker_t *)argument;
    for (int index = 0; index < worker->count; ++index) {
        ais_ingest_result_t result;
        ais_error_info_t error;
        ais_ingest_result_init(&result);
        ais_error_info_init(&error);
        if (ais_publish_f64(
                worker->ctx, worker->sensor, (double)index,
                &result, &error) != AIS_OK) {
            worker->failures += 1;
        }
    }
    return 0;
}

static int duplicate_worker_main(void *argument)
{
    duplicate_worker_t *worker = (duplicate_worker_t *)argument;
    ais_observation_view_t observation;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_observation_view_init(&observation);
    ais_ingest_result_init(&result);
    ais_error_info_init(&error);
    observation.sensor_id = worker->sensor;
    observation.sequence = 9999u;
    observation.payload.kind = AIS_PAYLOAD_REAL;
    observation.payload.as.real_value = 30.0;
    worker->result = (int)ais_publish_observation(
        worker->ctx, &observation, &result, &error);
    return 0;
}

int main(void)
{
    ais_ctx_t *ctx = NULL;
    ais_sensor_id_t sensor = 0u;
    ais_sensor_status_t status;
    ais_state_summary_t summary;
    ais_error_info_t error;
    thrd_t publishers[4];
    publish_worker_t workers[4];
    duplicate_worker_t duplicates[2];
    thrd_t duplicate_threads[2];
    int thread_result;

    ais_error_info_init(&error);
    if (ais_ctx_create_default(&ctx, &error) != AIS_OK ||
        ais_sensor_register_simple(
            ctx, AIS_SV("concurrent-temp"),
            AIS_SV("environment.temperature.v1"), AIS_SV("celsius"),
            &sensor, &error) != AIS_OK) {
        return 1;
    }
    for (size_t index = 0u; index < 4u; ++index) {
        workers[index].ctx = ctx;
        workers[index].sensor = sensor;
        workers[index].count = 100;
        workers[index].failures = 0;
        if (thrd_create(
                &publishers[index], publish_worker_main, &workers[index]) !=
            thrd_success) {
            return 1;
        }
    }
    for (size_t index = 0u; index < 4u; ++index) {
        if (thrd_join(publishers[index], &thread_result) != thrd_success ||
            workers[index].failures != 0) {
            return 1;
        }
    }
    for (size_t index = 0u; index < 2u; ++index) {
        duplicates[index].ctx = ctx;
        duplicates[index].sensor = sensor;
        duplicates[index].result = AIS_E_INTERNAL;
        if (thrd_create(
                &duplicate_threads[index], duplicate_worker_main,
                &duplicates[index]) != thrd_success) {
            return 1;
        }
    }
    for (size_t index = 0u; index < 2u; ++index) {
        if (thrd_join(duplicate_threads[index], &thread_result) !=
                thrd_success) {
            return 1;
        }
    }
    if (!((duplicates[0].result == AIS_OK &&
           duplicates[1].result == AIS_E_DUPLICATE) ||
          (duplicates[1].result == AIS_OK &&
           duplicates[0].result == AIS_E_DUPLICATE))) {
        fprintf(stderr, "same sequence was not serialized correctly\n");
        return 1;
    }
    ais_sensor_status_init(&status);
    if (ais_sensor_get_status(ctx, sensor, &status, &error) != AIS_OK ||
        status.observation_count != 401u || status.rejected_count != 1u) {
        return 1;
    }
    ais_state_summary_init(&summary);
    if (ais_ctx_get_state_summary(ctx, &summary, &error) != AIS_OK ||
        summary.recent_observation_count != 64u ||
        summary.rejected_observation_count != 1u) {
        return 1;
    }
    if (ais_ctx_close(&ctx, &error) != AIS_OK || ctx != NULL) {
        return 1;
    }
    return 0;
}
