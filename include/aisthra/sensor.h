/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_SENSOR_H
#define AISTHRA_SENSOR_H

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ais_ctx_config {
    ais_struct_header_t header;
    uint32_t max_recent_observations;
    uint32_t processing_mode;
    uint64_t stale_after_ns;
    uint64_t max_payload_bytes;
    uint32_t flags;
} ais_ctx_config_t;

typedef struct ais_sensor_config {
    ais_struct_header_t header;
    ais_string_view_t key;
    ais_string_view_t schema;
    ais_string_view_t unit;
    ais_string_view_t reference_frame;
    ais_string_view_t sensor_type;
    ais_string_view_t name;
    ais_string_view_t description;
    ais_string_view_t session_id;
    ais_string_view_t source_type;
    uint32_t modality;
    uint32_t acquisition_mode;
    uint32_t capabilities;
    uint8_t permission_required;
    double max_rate_hz;
    ais_provider_id_t provider_id;
} ais_sensor_config_t;

typedef struct ais_sensor_status {
    ais_struct_header_t header;
    uint32_t availability;
    uint32_t last_error;
    uint64_t observation_count;
    uint64_t rejected_count;
    uint64_t latest_sequence;
    uint8_t has_latest;
} ais_sensor_status_t;

AIS_API ais_status_t ais_ctx_config_init(ais_ctx_config_t *config);
AIS_API ais_status_t ais_sensor_config_init(ais_sensor_config_t *config);
AIS_API ais_status_t ais_sensor_status_init(ais_sensor_status_t *status);
/* A successful close consumes the handle and stores NULL in the caller's slot. */
AIS_API ais_status_t ais_ctx_create(
    const ais_ctx_config_t *config, ais_ctx_t **out_ctx,
    ais_error_info_t *error);
AIS_API ais_status_t ais_ctx_create_default(
    ais_ctx_t **out_ctx, ais_error_info_t *error);
AIS_API ais_status_t ais_ctx_close(
    ais_ctx_t **inout_ctx, ais_error_info_t *error);

AIS_API ais_status_t ais_sensor_register(
    ais_ctx_t *ctx, const ais_sensor_config_t *config,
    ais_sensor_id_t *out_sensor, ais_error_info_t *error);
AIS_API ais_status_t ais_sensor_register_simple(
    ais_ctx_t *ctx, ais_string_view_t key, ais_string_view_t schema,
    ais_string_view_t unit, ais_sensor_id_t *out_sensor,
    ais_error_info_t *error);
AIS_API ais_status_t ais_sensor_unregister(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, ais_error_info_t *error);
AIS_API ais_status_t ais_sensor_get_status(
    ais_ctx_t *ctx, ais_sensor_id_t sensor,
    ais_sensor_status_t *out_status, ais_error_info_t *error);

AIS_API ais_status_t ais_library_version(
    uint32_t *major, uint32_t *minor, uint32_t *patch);
AIS_API ais_status_t ais_api_version(uint32_t *major, uint32_t *minor);
AIS_API ais_status_t ais_abi_version(uint32_t *major, uint32_t *minor);
AIS_API ais_status_t ais_schema_version(uint32_t *major, uint32_t *minor);
AIS_API ais_status_t ais_feature_supported(
    uint32_t feature, uint8_t *out_supported);

#ifdef __cplusplus
}
#endif

#endif
