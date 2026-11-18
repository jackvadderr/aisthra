/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_ADVANCED_H
#define AISTHRA_ADVANCED_H

#include "aisthra.h"
#include "provider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ais_dedup_policy {
    ais_struct_header_t header;
    uint32_t history_limit;
    uint8_t reject_duplicates;
    uint8_t flag_out_of_order;
    uint8_t retain_dropped_predecessor_flag;
} ais_dedup_policy_t;

/* Callback arguments are borrowed and callback self-removal is rejected. */
typedef ais_status_t (*ais_schema_validate_fn)(
    void *user_data, const ais_observation_view_t *observation,
    ais_error_info_t *error);

typedef struct ais_schema_callbacks {
    ais_struct_header_t header;
    ais_schema_validate_fn validate;
    void (*destroy)(void *user_data);
} ais_schema_callbacks_t;

typedef struct ais_schema_config {
    ais_struct_header_t header;
    ais_string_view_t schema_id;
    uint32_t schema_major;
    uint32_t schema_minor;
    uint32_t modality;
    uint32_t allowed_payload_kinds;
    ais_string_view_t default_unit;
    ais_string_view_t default_reference_frame;
    ais_schema_callbacks_t callbacks;
    void *user_data;
} ais_schema_config_t;

typedef struct ais_event_view {
    ais_struct_header_t header;
    uint32_t type;
    uint32_t status;
    ais_sensor_id_t sensor_id;
    ais_provider_id_t provider_id;
    uint64_t sequence;
    uint32_t flags;
} ais_event_view_t;

typedef void (*ais_event_fn)(void *user_data, const ais_event_view_t *event);

enum {
    AIS_EVENT_ACCEPTED = UINT32_C(1),
    AIS_EVENT_REJECTED = UINT32_C(2),
    AIS_EVENT_DUPLICATE = UINT32_C(4),
    AIS_EVENT_PERCEPT_GENERATED = UINT32_C(8),
    AIS_EVENT_PROVIDER_AVAILABILITY = UINT32_C(16),
    AIS_EVENT_STALE_REFRESH = UINT32_C(32)
};

AIS_API ais_status_t ais_dedup_policy_init(ais_dedup_policy_t *policy);
AIS_API ais_status_t ais_schema_config_init(ais_schema_config_t *config);
AIS_API ais_status_t ais_ctx_set_dedup_policy(
    ais_ctx_t *ctx, const ais_dedup_policy_t *policy,
    ais_error_info_t *error);
AIS_API ais_status_t ais_process(
    ais_ctx_t *ctx, size_t max_items, size_t *out_processed,
    ais_error_info_t *error);
AIS_API ais_status_t ais_schema_register(
    ais_ctx_t *ctx, const ais_schema_config_t *config,
    ais_error_info_t *error);
AIS_API ais_status_t ais_schema_unregister(
    ais_ctx_t *ctx, ais_string_view_t schema_id, ais_error_info_t *error);
AIS_API ais_status_t ais_event_subscribe(
    ais_ctx_t *ctx, uint32_t event_mask, ais_event_fn callback,
    void *user_data, void (*destroy)(void *user_data),
    uint64_t *out_subscription, ais_error_info_t *error);
AIS_API ais_status_t ais_event_unsubscribe(
    ais_ctx_t *ctx, uint64_t subscription, ais_error_info_t *error);

#ifdef __cplusplus
}
#endif

#endif
