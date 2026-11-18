/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_TYPES_H
#define AISTHRA_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(AISTHRA_BUILD_SHARED)
#  if defined(AISTHRA_BUILDING)
#    define AIS_API __declspec(dllexport)
#  else
#    define AIS_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define AIS_API __attribute__((visibility("default")))
#else
#  define AIS_API
#endif

#define AIS_STRUCT_VERSION_1 UINT32_C(1)
#define AIS_SCHEMA_MAJOR UINT32_C(1)
#define AIS_SCHEMA_MINOR UINT32_C(0)

typedef uint32_t ais_status_t;
typedef uint64_t ais_sensor_id_t;
typedef uint64_t ais_provider_id_t;

/* Opaque handles are owned by the context lifecycle APIs. */
typedef struct ais_ctx ais_ctx_t;
typedef struct ais_provider ais_provider_t;
typedef struct ais_state_snapshot ais_state_snapshot_t;
typedef struct ais_observation_snapshot ais_observation_snapshot_t;
typedef struct ais_percept_snapshot ais_percept_snapshot_t;

typedef struct ais_struct_header {
    uint32_t size;
    uint32_t version;
} ais_struct_header_t;

/* Views borrow caller storage and never release their data. */
typedef struct ais_string_view {
    const char *data;
    size_t size;
} ais_string_view_t;

typedef struct ais_bytes_view {
    const void *data;
    size_t size;
} ais_bytes_view_t;

enum {
    AIS_MODALITY_INTERACTION = 1u,
    AIS_MODALITY_COMPUTATIONAL = 2u,
    AIS_MODALITY_SPATIAL = 3u,
    AIS_MODALITY_ENVIRONMENTAL = 4u,
    AIS_MODALITY_DERIVED = 5u
};

enum {
    AIS_CAP_READ = UINT32_C(1),
    AIS_CAP_EVENT = UINT32_C(2),
    AIS_CAP_STREAM = UINT32_C(4),
    AIS_CAP_QUALITY = UINT32_C(8),
    AIS_CAP_ACQUISITION_TIMESTAMP = UINT32_C(16),
    AIS_CAP_RECEIPT_TIMESTAMP = UINT32_C(32)
};

enum {
    AIS_ACQUISITION_POINT = 1u,
    AIS_ACQUISITION_EVENT = 2u,
    AIS_ACQUISITION_STREAM = 3u
};

enum {
    AIS_AVAIL_DISCOVERED = 1u,
    AIS_AVAIL_AVAILABLE = 2u,
    AIS_AVAIL_INITIALIZING = 3u,
    AIS_AVAIL_ACTIVE = 4u,
    AIS_AVAIL_DEGRADED = 5u,
    AIS_AVAIL_PERMISSION_DENIED = 6u,
    AIS_AVAIL_ERROR = 7u,
    AIS_AVAIL_STOPPING = 8u,
    AIS_AVAIL_STOPPED = 9u
};

enum {
    AIS_VALIDITY_VALID = 1u,
    AIS_VALIDITY_INVALID = 2u,
    AIS_VALIDITY_UNAVAILABLE = 3u,
    AIS_VALIDITY_STALE = 4u,
    AIS_VALIDITY_INFERRED = 5u,
    AIS_VALIDITY_INCOMPLETE = 6u
};

enum {
    AIS_CLOCK_PROCESS_MONOTONIC = 1u,
    AIS_CLOCK_SYSTEM_REALTIME = 2u,
    AIS_CLOCK_QT_MONOTONIC = 3u,
    AIS_CLOCK_COMPOSITOR = 4u,
    AIS_CLOCK_DEVICE_MONOTONIC = 5u,
    AIS_CLOCK_REMOTE_MONOTONIC = 6u,
    AIS_CLOCK_UNKNOWN = 7u
};

enum {
    AIS_UNIT_NONE = 0u,
    AIS_UNIT_PIXEL_LOGICAL = 1u,
    AIS_UNIT_METER = 2u,
    AIS_UNIT_MILLIMETER = 3u,
    AIS_UNIT_CELSIUS = 4u,
    AIS_UNIT_PERCENT = 5u,
    AIS_UNIT_NONE_DIMENSIONAL = 6u
};

enum {
    AIS_REF_NONE = 0u,
    AIS_REF_QT_LOCAL_LOGICAL = 1u,
    AIS_REF_SWAY_OUTPUT_LOGICAL = 2u,
    AIS_REF_DEVICE = 3u,
    AIS_REF_WORLD = 4u
};

enum {
    AIS_PAYLOAD_NONE = 0u,
    AIS_PAYLOAD_BOOL = 1u,
    AIS_PAYLOAD_INT = 2u,
    AIS_PAYLOAD_REAL = 3u,
    AIS_PAYLOAD_VECTOR = 4u,
    AIS_PAYLOAD_EVENT = 5u,
    AIS_PAYLOAD_STRUCT = 6u,
    AIS_PAYLOAD_BYTES = 7u,
    AIS_PAYLOAD_IMAGE_FRAME = 8u,
    AIS_PAYLOAD_AUDIO_BLOCK = 9u,
    AIS_PAYLOAD_REFERENCE = 10u
};

#define AIS_PAYLOAD_MASK(kind) (UINT32_C(1) << (kind))

enum {
    AIS_FLAG_NONE = UINT32_C(0),
    AIS_FLAG_OUT_OF_ORDER = UINT32_C(1),
    AIS_FLAG_DUPLICATE = UINT32_C(2),
    AIS_FLAG_DROPPED_PREDECESSOR = UINT32_C(4)
};

enum {
    AIS_PROCESS_ON_PUBLISH = 1u,
    AIS_PROCESS_MANUAL = 2u
};

enum {
    AIS_CTX_ALLOW_EXTERNAL_RECEIPT = UINT32_C(1)
};

typedef struct ais_timestamp {
    ais_struct_header_t header;
    double value_seconds;
    uint32_t domain;
    ais_string_view_t clock_id;
    uint8_t present;
    uint8_t reliable;
} ais_timestamp_t;

typedef struct ais_quality {
    ais_struct_header_t header;
    uint8_t known;
    double score;
    double age_seconds;
    uint64_t dropped_samples;
} ais_quality_t;

typedef struct ais_provenance {
    ais_struct_header_t header;
    ais_string_view_t source_type;
    ais_string_view_t provider_version;
    ais_string_view_t rule_or_model;
    ais_string_view_t source_observation_id;
} ais_provenance_t;

typedef struct ais_vector_value {
    double x;
    double y;
    double z;
} ais_vector_value_t;

typedef struct ais_event_value {
    ais_string_view_t name;
    ais_string_view_t value;
} ais_event_value_t;

typedef struct ais_structure_value {
    ais_string_view_t label;
    double x;
    double y;
    double width;
    double height;
    ais_string_view_t aux;
} ais_structure_value_t;

typedef union ais_payload_data {
    uint8_t bool_value;
    int64_t int_value;
    double real_value;
    ais_vector_value_t vector;
    ais_event_value_t event;
    ais_structure_value_t structure;
    ais_bytes_view_t bytes;
} ais_payload_data_t;

/* kind selects the active member of the discriminated payload union. */
typedef struct ais_payload_view {
    ais_struct_header_t header;
    uint32_t kind;
    ais_payload_data_t as;
} ais_payload_view_t;

AIS_API void ais_timestamp_init(ais_timestamp_t *timestamp);
AIS_API void ais_quality_init(ais_quality_t *quality);
AIS_API void ais_provenance_init(ais_provenance_t *provenance);
AIS_API void ais_payload_init(ais_payload_view_t *payload);
AIS_API ais_string_view_t ais_string_view_make(
    const char *data, size_t size);
AIS_API ais_string_view_t ais_string_view_from_cstr(const char *data);
AIS_API ais_status_t ais_timestamp_set(
    ais_timestamp_t *timestamp, double value_seconds, uint32_t domain,
    ais_string_view_t clock_id, uint8_t reliable);
AIS_API ais_status_t ais_payload_set_bool(
    ais_payload_view_t *payload, uint8_t value);
AIS_API ais_status_t ais_payload_set_int(
    ais_payload_view_t *payload, int64_t value);
AIS_API ais_status_t ais_payload_set_real(
    ais_payload_view_t *payload, double value);
AIS_API ais_status_t ais_payload_set_vector(
    ais_payload_view_t *payload, double x, double y, double z);
AIS_API ais_status_t ais_payload_set_event(
    ais_payload_view_t *payload, ais_string_view_t name,
    ais_string_view_t value);
AIS_API ais_status_t ais_payload_set_structure(
    ais_payload_view_t *payload, ais_string_view_t label,
    double x, double y, double width, double height,
    ais_string_view_t aux);
AIS_API ais_status_t ais_payload_set_bytes(
    ais_payload_view_t *payload, ais_bytes_view_t bytes);

#ifdef __cplusplus
}

template <size_t N>
constexpr ais_string_view_t ais_string_view_literal(
    const char (&text)[N]) noexcept
{
    static_assert(N > 0u, "string literal must include a terminator");
    return ais_string_view_t{ text, N - 1u };
}

constexpr ais_bytes_view_t ais_bytes_view_make(
    const void *data, size_t size) noexcept
{
    return ais_bytes_view_t{ data, size };
}

#define AIS_SV(text) ::ais_string_view_literal(text)
#define AIS_BYTES(data_ptr, data_size) \
    ::ais_bytes_view_make((data_ptr), (data_size))
#else
#define AIS_SV(text) \
    ((ais_string_view_t){ (text), sizeof(text) - (size_t)1u })
#define AIS_BYTES(data_ptr, data_size) \
    ((ais_bytes_view_t){ (data_ptr), (data_size) })
#endif

#endif
