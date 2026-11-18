/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_STATE_H
#define AISTHRA_STATE_H

#include "observation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Views obtained from a snapshot are valid only while that snapshot is held. */
typedef struct ais_state_summary {
    ais_struct_header_t header;
    uint64_t recent_observation_count;
    uint64_t active_percept_count;
    uint64_t unavailable_provider_count;
    uint64_t stale_observation_count;
    uint64_t rejected_observation_count;
    uint8_t processing_pending;
} ais_state_summary_t;

typedef struct ais_percept_view {
    ais_struct_header_t header;
    ais_string_view_t type;
    ais_payload_view_t payload;
    ais_string_view_t source_observation_id;
    ais_string_view_t rule_or_model;
    uint32_t validity;
    ais_quality_t quality;
    ais_timestamp_t generated_at;
    ais_string_view_t source_type;
    ais_provider_id_t source_provider_id;
    ais_sensor_id_t source_sensor_id;
    ais_string_view_t source_session_id;
    ais_string_view_t source_schema;
} ais_percept_view_t;

AIS_API void ais_state_summary_init(ais_state_summary_t *summary);
AIS_API void ais_percept_view_init(ais_percept_view_t *view);
AIS_API ais_status_t ais_ctx_get_state_summary(
    ais_ctx_t *ctx, ais_state_summary_t *out_state,
    ais_error_info_t *error);
AIS_API ais_status_t ais_ctx_refresh(
    ais_ctx_t *ctx, ais_error_info_t *error);
AIS_API ais_status_t ais_ctx_state_snapshot(
    ais_ctx_t *ctx, ais_state_snapshot_t **out_snapshot,
    ais_error_info_t *error);
AIS_API ais_status_t ais_state_snapshot_release(
    ais_state_snapshot_t **inout_snapshot, ais_error_info_t *error);
AIS_API ais_status_t ais_state_snapshot_observation_count(
    const ais_state_snapshot_t *snapshot, size_t *out_count,
    ais_error_info_t *error);
AIS_API ais_status_t ais_state_snapshot_observation_at(
    const ais_state_snapshot_t *snapshot, size_t index,
    ais_observation_snapshot_t **out_observation,
    ais_error_info_t *error);
AIS_API ais_status_t ais_state_snapshot_percept_count(
    const ais_state_snapshot_t *snapshot, size_t *out_count,
    ais_error_info_t *error);
AIS_API ais_status_t ais_state_snapshot_percept_at(
    const ais_state_snapshot_t *snapshot, size_t index,
    ais_percept_snapshot_t **out_percept, ais_error_info_t *error);
AIS_API ais_status_t ais_percept_snapshot_get_view(
    const ais_percept_snapshot_t *snapshot,
    ais_percept_view_t *out_view, ais_error_info_t *error);
AIS_API ais_status_t ais_percept_snapshot_release(
    ais_percept_snapshot_t **inout_percept, ais_error_info_t *error);

#ifdef __cplusplus
}
#endif

#endif
