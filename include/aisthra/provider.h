/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_PROVIDER_H
#define AISTHRA_PROVIDER_H

#include "observation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callback user_data remains application-owned until destroy is called. */
typedef ais_status_t (*ais_provider_start_fn)(void *user_data);
typedef ais_status_t (*ais_provider_stop_fn)(void *user_data);
typedef ais_status_t (*ais_provider_emit_fn)(
    void *emit_context, const ais_observation_view_t *observation,
    ais_ingest_result_t *result, ais_error_info_t *error);
typedef ais_status_t (*ais_provider_poll_fn)(
    void *user_data, ais_provider_emit_fn emit, void *emit_context);
typedef void (*ais_provider_destroy_fn)(void *user_data);

typedef struct ais_provider_vtable {
    ais_struct_header_t header;
    ais_provider_start_fn start;
    ais_provider_stop_fn stop;
    ais_provider_poll_fn poll;
    ais_provider_destroy_fn destroy;
} ais_provider_vtable_t;

typedef struct ais_provider_config {
    ais_struct_header_t header;
    ais_string_view_t key;
    ais_string_view_t version;
    uint32_t capabilities;
    ais_provider_vtable_t vtable;
    void *user_data;
} ais_provider_config_t;

typedef struct ais_provider_status {
    ais_struct_header_t header;
    uint32_t availability;
    uint32_t last_error;
    uint64_t observation_count;
    uint64_t rejected_count;
    uint8_t started;
} ais_provider_status_t;

AIS_API ais_status_t ais_provider_config_init(ais_provider_config_t *config);
AIS_API ais_status_t ais_provider_status_init(ais_provider_status_t *status);
AIS_API ais_status_t ais_provider_register(
    ais_ctx_t *ctx, const ais_provider_config_t *config,
    ais_provider_id_t *out_provider, ais_error_info_t *error);
AIS_API ais_status_t ais_provider_start(
    ais_ctx_t *ctx, ais_provider_id_t provider, ais_error_info_t *error);
AIS_API ais_status_t ais_provider_poll(
    ais_ctx_t *ctx, ais_provider_id_t provider, ais_error_info_t *error);
AIS_API ais_status_t ais_provider_stop(
    ais_ctx_t *ctx, ais_provider_id_t provider, ais_error_info_t *error);
AIS_API ais_status_t ais_provider_set_availability(
    ais_ctx_t *ctx, ais_provider_id_t provider, uint32_t availability,
    uint32_t error_code, ais_error_info_t *error);
AIS_API ais_status_t ais_provider_get_status(
    ais_ctx_t *ctx, ais_provider_id_t provider,
    ais_provider_status_t *out_status, ais_error_info_t *error);
AIS_API ais_status_t ais_provider_unregister(
    ais_ctx_t *ctx, ais_provider_id_t provider, ais_error_info_t *error);

#ifdef __cplusplus
}
#endif

#endif
