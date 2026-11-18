/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_OBSERVATION_H
#define AISTHRA_OBSERVATION_H

#include "sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ais_ingest_result {
    ais_struct_header_t header;
    uint32_t disposition;
    uint32_t flags;
    uint64_t sequence;
    ais_string_view_t observation_id;
} ais_ingest_result_t;

enum {
    AIS_INGEST_ACCEPTED = 1u,
    AIS_INGEST_REJECTED = 2u,
    AIS_INGEST_DUPLICATE = 3u,
    AIS_INGEST_QUEUED = 4u
};

/* Input views are borrowed for the duration of validation or publication. */
typedef struct ais_observation_view {
    ais_struct_header_t header;
    ais_string_view_t observation_id;
    ais_sensor_id_t sensor_id;
    ais_provider_id_t provider_id;
    ais_string_view_t session_id;
    ais_string_view_t observation_type;
    uint32_t acquisition;
    ais_timestamp_t acquired_at;
    ais_timestamp_t received_at;
    uint64_t sequence;
    ais_payload_view_t payload;
    uint32_t unit_code;
    ais_string_view_t unit_id;
    uint32_t reference_frame_code;
    ais_string_view_t reference_frame_id;
    uint32_t validity;
    ais_quality_t quality;
    ais_provenance_t provenance;
} ais_observation_view_t;

AIS_API void ais_ingest_result_init(ais_ingest_result_t *result);
AIS_API void ais_observation_view_init(ais_observation_view_t *observation);

AIS_API ais_status_t ais_publish_f64(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, double value,
    ais_ingest_result_t *result, ais_error_info_t *error);
AIS_API ais_status_t ais_publish_i64(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, int64_t value,
    ais_ingest_result_t *result, ais_error_info_t *error);
AIS_API ais_status_t ais_publish_bool(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, uint8_t value,
    ais_ingest_result_t *result, ais_error_info_t *error);
AIS_API ais_status_t ais_publish_bytes(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, ais_bytes_view_t value,
    ais_ingest_result_t *result, ais_error_info_t *error);

AIS_API ais_status_t ais_latest_f64(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, double *out_value,
    ais_error_info_t *error);
AIS_API ais_status_t ais_latest_i64(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, int64_t *out_value,
    ais_error_info_t *error);
AIS_API ais_status_t ais_latest_bool(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, uint8_t *out_value,
    ais_error_info_t *error);
AIS_API ais_status_t ais_latest_bytes(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, void *buffer,
    size_t capacity, size_t *out_size, ais_error_info_t *error);

AIS_API ais_status_t ais_observation_validate(
    ais_ctx_t *ctx, const ais_observation_view_t *observation,
    ais_error_info_t *error);
AIS_API ais_status_t ais_publish_observation(
    ais_ctx_t *ctx, const ais_observation_view_t *observation,
    ais_ingest_result_t *result, ais_error_info_t *error);

/* Snapshot handles own copied data and require an explicit release call. */
AIS_API ais_status_t ais_sensor_latest_observation(
    ais_ctx_t *ctx, ais_sensor_id_t sensor,
    ais_observation_snapshot_t **out_snapshot,
    ais_error_info_t *error);
AIS_API ais_status_t ais_observation_snapshot_get_view(
    const ais_observation_snapshot_t *snapshot,
    ais_observation_view_t *out_view, ais_error_info_t *error);
AIS_API ais_status_t ais_observation_snapshot_release(
    ais_observation_snapshot_t **inout_snapshot,
    ais_error_info_t *error);

#ifdef __cplusplus
}
#endif

#endif
