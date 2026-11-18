/* SPDX-License-Identifier: MIT */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <aisthra/advanced.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "internal/aisthra_platform.h"

#if defined(AISTHRA_TESTING)
static atomic_long ais_test_alloc_remaining = -1;

static int ais_test_should_fail_allocation(void)
{
    long remaining = atomic_load_explicit(
        &ais_test_alloc_remaining, memory_order_relaxed);
    for (;;) {
        if (remaining < 0) {
            return 0;
        }
        if (remaining == 0) {
            return 1;
        }
        if (atomic_compare_exchange_weak_explicit(
            &ais_test_alloc_remaining, &remaining, remaining - 1,
                memory_order_relaxed, memory_order_relaxed)) {
            return 0;
        }
    }
}

static void *ais_test_malloc(size_t size)
{
    return ais_test_should_fail_allocation() ? NULL : malloc(size);
}

static void *ais_test_calloc(size_t count, size_t size)
{
    return ais_test_should_fail_allocation() ? NULL : calloc(count, size);
}

static void *ais_test_realloc(void *pointer, size_t size)
{
    return ais_test_should_fail_allocation() ? NULL : realloc(pointer, size);
}

void ais_test_alloc_reset(void)
{
    atomic_store_explicit(
        &ais_test_alloc_remaining, -1, memory_order_relaxed);
}

void ais_test_alloc_fail_after(long allocations)
{
    atomic_store_explicit(
        &ais_test_alloc_remaining, allocations, memory_order_relaxed);
}

#define malloc ais_test_malloc
#define calloc ais_test_calloc
#define realloc ais_test_realloc
#endif

#define AIS_DEFAULT_MAX_RECENT ((size_t)64u)
#define AIS_DEFAULT_MAX_PAYLOAD ((size_t)1024u * (size_t)1024u)
#define AIS_DEFAULT_DEDUP_HISTORY ((size_t)32u)
#define AIS_DEFAULT_STALE_NS UINT64_C(5000000000)
#define AIS_MAX_TEXT_COPY ((size_t)4096u)

typedef struct ais_owned_string {
    char *data;
    size_t size;
} ais_owned_string_t;

typedef struct ais_owned_timestamp {
    double value_seconds;
    uint32_t domain;
    ais_owned_string_t clock_id;
    uint8_t present;
    uint8_t reliable;
} ais_owned_timestamp_t;

typedef struct ais_owned_payload {
    uint32_t kind;
    union {
        uint8_t bool_value;
        int64_t int_value;
        double real_value;
        ais_vector_value_t vector;
        struct {
            ais_owned_string_t name;
            ais_owned_string_t value;
        } event;
        struct {
            ais_owned_string_t label;
            double x;
            double y;
            double width;
            double height;
            ais_owned_string_t aux;
        } structure;
        struct {
            void *data;
            size_t size;
        } bytes;
    } as;
} ais_owned_payload_t;

typedef struct ais_owned_quality {
    uint8_t known;
    double score;
    double age_seconds;
    uint64_t dropped_samples;
} ais_owned_quality_t;

typedef struct ais_owned_provenance {
    ais_owned_string_t source_type;
    ais_owned_string_t provider_version;
    ais_owned_string_t rule_or_model;
    ais_owned_string_t source_observation_id;
} ais_owned_provenance_t;

typedef struct ais_owned_observation {
    ais_owned_string_t observation_id;
    ais_sensor_id_t sensor_id;
    ais_provider_id_t provider_id;
    ais_owned_string_t session_id;
    ais_owned_string_t observation_type;
    uint32_t acquisition;
    ais_owned_timestamp_t acquired_at;
    ais_owned_timestamp_t received_at;
    uint64_t sequence;
    ais_owned_payload_t payload;
    uint32_t unit_code;
    ais_owned_string_t unit_id;
    uint32_t reference_frame_code;
    ais_owned_string_t reference_frame_id;
    uint32_t validity;
    ais_owned_quality_t quality;
    ais_owned_provenance_t provenance;
    uint32_t flags;
} ais_owned_observation_t;

typedef struct ais_owned_percept {
    ais_owned_string_t type;
    ais_owned_payload_t payload;
    ais_owned_string_t source_observation_id;
    ais_owned_string_t rule_or_model;
    uint32_t validity;
    ais_owned_quality_t quality;
    ais_owned_timestamp_t generated_at;
    ais_owned_string_t source_type;
    ais_provider_id_t source_provider_id;
    ais_sensor_id_t source_sensor_id;
    ais_owned_string_t source_session_id;
    ais_owned_string_t source_schema;
} ais_owned_percept_t;

typedef struct ais_callback_guard ais_callback_guard_t;

typedef struct ais_provider_emit_context {
    ais_ctx_t *ctx;
    ais_provider_id_t provider_id;
    atomic_bool active;
} ais_provider_emit_context_t;

typedef struct ais_provider_entry {
    ais_provider_id_t id;
    ais_owned_string_t key;
    ais_owned_string_t version;
    uint32_t capabilities;
    ais_provider_vtable_t vtable;
    void *user_data;
    uint32_t availability;
    uint32_t last_error;
    uint64_t observation_count;
    uint64_t rejected_count;
    uint8_t started;
    uint8_t implicit;
    uint8_t poll_in_progress;
    uint8_t lifecycle_in_progress;
    ais_provider_emit_context_t *emit_context;
} ais_provider_entry_t;

typedef struct ais_sensor_entry {
    ais_sensor_id_t id;
    ais_owned_string_t key;
    ais_owned_string_t schema;
    ais_owned_string_t unit_id;
    ais_owned_string_t reference_frame_id;
    ais_owned_string_t sensor_type;
    ais_owned_string_t name;
    ais_owned_string_t description;
    ais_owned_string_t session_id;
    ais_owned_string_t source_type;
    uint32_t modality;
    uint32_t acquisition_mode;
    uint32_t capabilities;
    uint8_t permission_required;
    double max_rate_hz;
    ais_provider_id_t provider_id;
    uint32_t unit_code;
    uint32_t reference_frame_code;
    uint32_t availability;
    uint32_t last_error;
    uint64_t observation_count;
    uint64_t rejected_count;
    uint64_t next_sequence;
    uint64_t highest_sequence;
    uint8_t sequence_exhausted;
    uint8_t has_highest_sequence;
    uint8_t has_latest;
    ais_owned_observation_t latest;
    uint64_t *sequence_history;
    size_t sequence_count;
    size_t sequence_capacity;
} ais_sensor_entry_t;

typedef struct ais_schema_entry {
    ais_owned_string_t schema_id;
    uint32_t major;
    uint32_t minor;
    uint32_t modality;
    uint32_t allowed_payload_kinds;
    ais_owned_string_t default_unit;
    ais_owned_string_t default_reference_frame;
    ais_schema_callbacks_t callbacks;
    void *user_data;
    size_t in_flight;
    uint8_t active;
} ais_schema_entry_t;

typedef struct ais_event_entry {
    uint64_t id;
    uint32_t mask;
    ais_event_fn callback;
    void *user_data;
    void (*destroy)(void *);
    size_t in_flight;
    uint8_t active;
} ais_event_entry_t;

typedef struct ais_lifetime {
    atomic_uint references;
    atomic_bool closed;
} ais_lifetime_t;

struct ais_observation_snapshot {
    ais_lifetime_t *lifetime;
    ais_owned_observation_t observation;
};

struct ais_percept_snapshot {
    ais_lifetime_t *lifetime;
    ais_owned_percept_t percept;
};

struct ais_state_snapshot {
    ais_lifetime_t *lifetime;
    ais_owned_observation_t *observations;
    size_t observation_count;
    ais_owned_percept_t *percepts;
    size_t percept_count;
};

struct ais_ctx {
    mtx_t mutex;
    cnd_t condition;
    ais_lifetime_t *lifetime;
    uint8_t mutex_ready;
    uint8_t closing;
    size_t active_calls;
    ais_callback_guard_t *callback_frames;
    size_t max_recent;
    size_t max_payload;
    size_t dedup_history_limit;
    uint8_t reject_duplicates;
    uint8_t flag_out_of_order;
    uint8_t retain_dropped_predecessor_flag;
    uint32_t processing_mode;
    uint32_t flags;
    uint64_t stale_after_ns;
    uint64_t rejected_observation_count;
    uint64_t stale_observation_count;
    ais_owned_string_t default_session;
    uint64_t next_sensor_id;
    uint64_t next_provider_id;
    uint64_t next_observation_id;
    uint64_t next_subscription_id;
    ais_owned_string_t last_result_id;
    ais_provider_entry_t *providers;
    size_t provider_count;
    size_t provider_capacity;
    ais_sensor_entry_t *sensors;
    size_t sensor_count;
    size_t sensor_capacity;
    ais_schema_entry_t **schemas;
    size_t schema_count;
    size_t schema_capacity;
    ais_owned_observation_t *recent;
    size_t recent_count;
    size_t recent_capacity;
    ais_owned_percept_t *percepts;
    size_t percept_count;
    size_t percept_capacity;
    ais_owned_observation_t *pending;
    size_t pending_count;
    size_t pending_capacity;
    ais_event_entry_t **events;
    size_t event_count;
    size_t event_capacity;
};

struct ais_callback_guard {
    ais_ctx_t *context;
    aisthra_thread_id_t thread;
    uint32_t kind;
    ais_callback_guard_t *next;
    uint8_t active;
};

typedef struct ais_builtin_schema {
    const char *id;
    uint32_t modality;
    uint32_t payload_mask;
    uint32_t unit_code;
    const char *unit_id;
    uint32_t reference_frame_code;
    const char *reference_frame_id;
    uint32_t acquisition;
    uint32_t capabilities;
} ais_builtin_schema_t;

enum {
    AIS_CALLBACK_NONE = 0u,
    AIS_CALLBACK_SCHEMA = 1u,
    AIS_CALLBACK_EVENT = 2u,
    AIS_CALLBACK_PROVIDER = 3u
};

static void ais_callback_enter(
    ais_ctx_t *context, uint32_t kind, ais_callback_guard_t *guard)
{
    if (guard == NULL) {
        return;
    }
    guard->context = context;
    guard->thread = aisthra_thread_current();
    guard->kind = kind;
    guard->next = NULL;
    guard->active = 0u;
    if (context == NULL || !context->mutex_ready ||
        mtx_lock(&context->mutex) != thrd_success) {
        return;
    }
    guard->next = context->callback_frames;
    context->callback_frames = guard;
    guard->active = 1u;
    (void)mtx_unlock(&context->mutex);
}

static void ais_callback_leave(ais_callback_guard_t *guard)
{
    ais_callback_guard_t **cursor;
    if (guard == NULL || !guard->active || guard->context == NULL ||
        !guard->context->mutex_ready ||
        mtx_lock(&guard->context->mutex) != thrd_success) {
        return;
    }
    cursor = &guard->context->callback_frames;
    while (*cursor != NULL && *cursor != guard) {
        cursor = &(*cursor)->next;
    }
    if (*cursor == guard) {
        *cursor = guard->next;
        guard->active = 0u;
    }
    (void)mtx_unlock(&guard->context->mutex);
}

static int ais_callback_active(ais_ctx_t *context, uint32_t kind)
{
    ais_callback_guard_t *frame;
    aisthra_thread_id_t thread;
    int active = 0;
    if (context == NULL || !context->mutex_ready ||
        mtx_lock(&context->mutex) != thrd_success) {
        return 0;
    }
    thread = aisthra_thread_current();
    for (frame = context->callback_frames; frame != NULL; frame = frame->next) {
        if (aisthra_thread_equal(frame->thread, thread) &&
            (kind == AIS_CALLBACK_NONE || frame->kind == kind)) {
            active = 1;
            break;
        }
    }
    (void)mtx_unlock(&context->mutex);
    return active;
}

/* Owned fields are released by their enclosing object's destructor. */
static void ais_owned_string_free(ais_owned_string_t *value);
static int ais_owned_string_set(
    ais_owned_string_t *target, ais_string_view_t source);
static int ais_owned_string_clone(
    ais_owned_string_t *target, const ais_owned_string_t *source);
static int ais_owned_string_set_literal(
    ais_owned_string_t *target, const char *source);
static void ais_owned_observation_free(ais_owned_observation_t *observation);
static void ais_owned_percept_free(ais_owned_percept_t *percept);
static int ais_owned_observation_clone(
    ais_owned_observation_t *target, const ais_owned_observation_t *source);
static int ais_owned_percept_clone(
    ais_owned_percept_t *target, const ais_owned_percept_t *source);
static void ais_schema_entry_free(ais_schema_entry_t *schema);
/* A live-call reference keeps context storage alive during an API operation. */
static int ais_ctx_enter(ais_ctx_t *ctx);
static void ais_ctx_leave(ais_ctx_t *ctx);
static ais_status_t ais_commit_observation_locked(
    ais_ctx_t *ctx, ais_sensor_entry_t *sensor,
    ais_provider_entry_t *provider, ais_owned_observation_t *observation,
    uint8_t *percept_generated);
static void ais_dispatch_event(
    ais_ctx_t *ctx, uint32_t type, ais_status_t status,
    ais_sensor_id_t sensor_id, ais_provider_id_t provider_id,
    uint64_t sequence, uint32_t flags);
static void ais_mark_rejected_locked(
    ais_ctx_t *ctx, ais_sensor_entry_t *sensor,
    ais_provider_entry_t *provider);
static ais_status_t ais_provider_emit_trampoline(
    void *emit_context, const ais_observation_view_t *observation,
    ais_ingest_result_t *result, ais_error_info_t *error);
static int ais_text_valid(ais_string_view_t value, uint8_t required);
static int ais_bytes_valid(ais_bytes_view_t value);
static int ais_pointer_event_bytes_valid(
    ais_bytes_view_t value, uint32_t expected_event_type);
static uint32_t ais_desktop_schema_kind(ais_string_view_t schema);
static int ais_desktop_bytes_valid(
    ais_bytes_view_t value, uint32_t expected_kind);

static uint32_t ais_pointer_schema_event_type(ais_string_view_t schema)
{
    static const char *names[] = {
        NULL,
        "interaction.pointer_press.v1",
        "interaction.pointer_move_local.v1",
        "interaction.pointer_release.v1",
        "interaction.pointer_deactivate.v1",
        "interaction.pointer_enter.v1",
        "interaction.pointer_leave.v1"
    };
    for (uint32_t event_type = 1u; event_type <= 6u; ++event_type) {
        size_t size = strlen(names[event_type]);
        if (schema.data != NULL && schema.size == size &&
            memcmp(schema.data, names[event_type], size) == 0) {
            return event_type;
        }
    }
    return 0u;
}

static uint32_t ais_desktop_schema_kind(ais_string_view_t schema)
{
    static const char *names[] = {
        NULL,
        "desktop.window_geometry.v1",
        "desktop.workspace_snapshot.v1",
        "desktop.output_geometry.v1"
    };
    for (uint32_t kind = 1u; kind <= 3u; ++kind) {
        size_t size = strlen(names[kind]);
        if (schema.data != NULL && schema.size == size &&
            memcmp(schema.data, names[kind], size) == 0) {
            return kind;
        }
    }
    return 0u;
}
static int ais_payload_view_well_formed(
    const ais_payload_view_t *payload, size_t max_bytes);
static int ais_array_grow(
    void **array, size_t *capacity, size_t needed, size_t element_size);
static ais_string_view_t ais_owned_string_view(
    const ais_owned_string_t *value);
static uint8_t ais_double_valid(double value);
static uint8_t ais_domain_valid(uint32_t domain);
static uint8_t ais_timestamp_valid(
    const ais_owned_timestamp_t *timestamp);
static uint8_t ais_quality_valid(const ais_owned_quality_t *quality);
static void ais_owned_timestamp_free(ais_owned_timestamp_t *timestamp);
static int ais_owned_timestamp_copy(
    ais_owned_timestamp_t *target, const ais_timestamp_t *source);
static int ais_owned_timestamp_clone(
    ais_owned_timestamp_t *target, const ais_owned_timestamp_t *source);
static int ais_owned_provenance_copy(
    ais_owned_provenance_t *target, const ais_provenance_t *source);
static int ais_owned_provenance_clone(
    ais_owned_provenance_t *target,
    const ais_owned_provenance_t *source);
static void ais_owned_payload_free(ais_owned_payload_t *payload);
static int ais_owned_payload_copy(
    ais_owned_payload_t *target, const ais_payload_view_t *source,
    size_t max_bytes);
static int ais_owned_payload_clone(
    ais_owned_payload_t *target, const ais_owned_payload_t *source);
static ais_lifetime_t *ais_lifetime_create(void);
static void ais_lifetime_retain(ais_lifetime_t *lifetime);
static void ais_lifetime_release(ais_lifetime_t *lifetime);
static uint8_t ais_lifetime_closed(const ais_lifetime_t *lifetime);
static int ais_now_timestamp(ais_owned_timestamp_t *timestamp);
static uint8_t ais_timestamps_comparable(
    const ais_owned_timestamp_t *left, const ais_owned_timestamp_t *right);
static uint64_t ais_age_nanoseconds(
    const ais_owned_timestamp_t *now,
    const ais_owned_timestamp_t *received);
static void ais_error_clear(ais_error_info_t *error);
static ais_status_t ais_error_set(
    ais_error_info_t *error, ais_status_t status, uint8_t retryable,
    const char *provider, const char *sensor, const char *operation,
    const char *message);
static ais_status_t ais_error_set_entry(
    ais_error_info_t *error, ais_status_t status, uint8_t retryable,
    const ais_provider_entry_t *provider, const ais_sensor_entry_t *sensor,
    const char *operation, const char *message);
static const char *ais_status_message(ais_status_t status);
static const ais_builtin_schema_t *ais_builtin_find(ais_string_view_t id);
static ais_schema_entry_t *ais_custom_schema_find(
    ais_ctx_t *ctx, ais_string_view_t id);
static ais_provider_entry_t *ais_provider_find(
    ais_ctx_t *ctx, ais_provider_id_t id);
static ais_sensor_entry_t *ais_sensor_find(
    ais_ctx_t *ctx, ais_sensor_id_t id);
static uint32_t ais_unit_code(ais_string_view_t value);
static uint32_t ais_reference_code(ais_string_view_t value);
static int ais_string_equal(
    const ais_owned_string_t *left, ais_string_view_t right);
static int ais_owned_string_equal(
    const ais_owned_string_t *left, const ais_owned_string_t *right);
static int ais_schema_resolve(
    ais_ctx_t *ctx, ais_string_view_t schema_id,
    const ais_builtin_schema_t **builtin, ais_schema_entry_t **custom);
static uint8_t ais_schema_callback_begin_locked(
    ais_schema_entry_t *schema);
static void ais_schema_callback_end(
    ais_ctx_t *ctx, ais_schema_entry_t *schema);

static void ais_header_init(ais_struct_header_t *header, size_t size)
{
    if (header != NULL) {
        header->size = (uint32_t)size;
        header->version = AIS_STRUCT_VERSION_1;
    }
}

static int ais_header_valid(
    const ais_struct_header_t *header, size_t minimum_size)
{
    return header != NULL &&
        header->version == AIS_STRUCT_VERSION_1 &&
        header->size >= minimum_size;
}

static int ais_utf8_valid(ais_string_view_t value)
{
    size_t index = 0u;
    while (index < value.size) {
        uint8_t first = (uint8_t)value.data[index];
        size_t extra;
        uint32_t codepoint;
        if (first < 0x80u) {
            index += 1u;
            continue;
        }
        if (first >= 0xC2u && first <= 0xDFu) {
            extra = 1u;
            codepoint = first & 0x1Fu;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            extra = 2u;
            codepoint = first & 0x0Fu;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            extra = 3u;
            codepoint = first & 0x07u;
        } else {
            return 0;
        }
        if (extra > value.size - index - 1u) {
            return 0;
        }
        for (size_t offset = 1u; offset <= extra; ++offset) {
            uint8_t continuation = (uint8_t)value.data[index + offset];
            if ((continuation & 0xC0u) != 0x80u) {
                return 0;
            }
            codepoint = (codepoint << 6u) | (continuation & 0x3Fu);
        }
        if ((extra == 2u && codepoint < 0x800u) ||
            (extra == 3u && codepoint < 0x10000u) ||
            codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            return 0;
        }
        index += extra + 1u;
    }
    return 1;
}

static int ais_schema_config_view(
    const ais_schema_config_t *source, ais_schema_config_t *target)
{
    size_t available;
    if (source == NULL || target == NULL ||
        !ais_header_valid(&source->header, sizeof(ais_struct_header_t))) {
        return 0;
    }
    (void)ais_schema_config_init(target);
    available = source->header.size;
#define AIS_COPY_SCHEMA_FIELD(field) \
    if (available >= offsetof(ais_schema_config_t, field) + sizeof(source->field)) \
        target->field = source->field
    AIS_COPY_SCHEMA_FIELD(schema_id);
    AIS_COPY_SCHEMA_FIELD(schema_major);
    AIS_COPY_SCHEMA_FIELD(schema_minor);
    AIS_COPY_SCHEMA_FIELD(modality);
    AIS_COPY_SCHEMA_FIELD(allowed_payload_kinds);
    AIS_COPY_SCHEMA_FIELD(default_unit);
    AIS_COPY_SCHEMA_FIELD(default_reference_frame);
    AIS_COPY_SCHEMA_FIELD(callbacks);
    AIS_COPY_SCHEMA_FIELD(user_data);
#undef AIS_COPY_SCHEMA_FIELD
    return 1;
}

static uint32_t ais_known_event_mask(void)
{
    return AIS_EVENT_ACCEPTED | AIS_EVENT_REJECTED |
        AIS_EVENT_DUPLICATE | AIS_EVENT_PERCEPT_GENERATED |
        AIS_EVENT_PROVIDER_AVAILABILITY | AIS_EVENT_STALE_REFRESH;
}

AIS_API ais_status_t ais_dedup_policy_init(ais_dedup_policy_t *policy)
{
    if (policy == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    memset(policy, 0, sizeof(*policy));
    ais_header_init(&policy->header, sizeof(*policy));
    policy->history_limit = (uint32_t)AIS_DEFAULT_DEDUP_HISTORY;
    policy->reject_duplicates = 1u;
    policy->flag_out_of_order = 1u;
    policy->retain_dropped_predecessor_flag = 0u;
    return AIS_OK;
}

AIS_API ais_status_t ais_schema_config_init(ais_schema_config_t *config)
{
    if (config == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    memset(config, 0, sizeof(*config));
    ais_header_init(&config->header, sizeof(*config));
    config->schema_major = AIS_SCHEMA_MAJOR;
    config->schema_minor = AIS_SCHEMA_MINOR;
    ais_header_init(&config->callbacks.header, sizeof(config->callbacks));
    return AIS_OK;
}

AIS_API ais_status_t ais_ctx_set_dedup_policy(
    ais_ctx_t *ctx, const ais_dedup_policy_t *policy,
    ais_error_info_t *error)
{
    ais_dedup_policy_t normalized;
    size_t available;
    if (policy == NULL ||
        !ais_header_valid(&policy->header, sizeof(ais_struct_header_t))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "dedup_policy", "invalid policy header");
    }
    (void)ais_dedup_policy_init(&normalized);
    available = policy->header.size;
#define AIS_COPY_DEDUP_FIELD(field) \
    if (available >= offsetof(ais_dedup_policy_t, field) + sizeof(policy->field)) \
        normalized.field = policy->field
    AIS_COPY_DEDUP_FIELD(history_limit);
    AIS_COPY_DEDUP_FIELD(reject_duplicates);
    AIS_COPY_DEDUP_FIELD(flag_out_of_order);
    AIS_COPY_DEDUP_FIELD(retain_dropped_predecessor_flag);
#undef AIS_COPY_DEDUP_FIELD
    if (normalized.reject_duplicates > 1u ||
        normalized.flag_out_of_order > 1u ||
        normalized.retain_dropped_predecessor_flag > 1u) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "dedup_policy", "boolean policy field is invalid");
    }
    if (normalized.history_limit == 0u) {
        normalized.history_limit = (uint32_t)AIS_DEFAULT_DEDUP_HISTORY;
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "dedup_policy", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "dedup_policy", "context is closing");
    }
    if (ctx->recent_count != 0u || ctx->pending_count != 0u) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_CONFLICT, 0u, NULL, NULL,
            "dedup_policy", "policy cannot change after ingestion");
    }
    ctx->dedup_history_limit = normalized.history_limit;
    ctx->reject_duplicates = normalized.reject_duplicates;
    ctx->flag_out_of_order = normalized.flag_out_of_order;
    ctx->retain_dropped_predecessor_flag =
        normalized.retain_dropped_predecessor_flag;
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

static void ais_sensor_advance_sequence_locked(
    ais_sensor_entry_t *sensor, uint64_t sequence)
{
    if (sensor == NULL) {
        return;
    }
    if (sequence == UINT64_MAX) {
        sensor->sequence_exhausted = 1u;
    } else if (sequence >= sensor->next_sequence) {
        sensor->next_sequence = sequence + 1u;
    }
}

AIS_API ais_status_t ais_process(
    ais_ctx_t *ctx, size_t max_items, size_t *out_processed,
    ais_error_info_t *error)
{
    size_t processed = 0u;
    size_t limit = max_items == 0u ? SIZE_MAX : max_items;
    if (out_processed == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "process", "missing processed output");
    }
    *out_processed = 0u;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "process", "context is unavailable");
    }
    for (;;) {
        ais_owned_observation_t committed;
        ais_owned_observation_t *pending;
        ais_sensor_entry_t *sensor;
        ais_provider_entry_t *provider;
        ais_status_t status;
        uint8_t percept_generated = 0u;
        ais_sensor_id_t sensor_id;
        ais_provider_id_t provider_id;
        uint64_t sequence;
        uint32_t flags;
        ais_status_t process_status;
        if (processed >= limit) {
            break;
        }
        memset(&committed, 0, sizeof(committed));
        (void)mtx_lock(&ctx->mutex);
        if (ctx->closing) {
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            *out_processed = processed;
            return ais_error_set(
                error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
                "process", "context is closing");
        }
        if (ctx->processing_mode != AIS_PROCESS_MANUAL) {
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            *out_processed = processed;
            return ais_error_set(
                error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
                "process", "context is not in manual mode");
        }
        if (ctx->pending_count == 0u) {
            (void)mtx_unlock(&ctx->mutex);
            break;
        }
        pending = &ctx->pending[0];
        sensor = ais_sensor_find(ctx, pending->sensor_id);
        provider = ais_provider_find(ctx, pending->provider_id);
        if (sensor == NULL || provider == NULL) {
            ais_owned_observation_free(pending);
            memmove(
                &ctx->pending[0], &ctx->pending[1],
                (ctx->pending_count - 1u) * sizeof(*ctx->pending));
            ctx->pending_count -= 1u;
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            *out_processed = processed;
            return ais_error_set(
                error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
                "process", "queued identity disappeared");
        }
        status = ais_commit_observation_locked(
            ctx, sensor, provider, pending, &percept_generated);
        sensor_id = sensor->id;
        provider_id = provider->id;
        sequence = pending->sequence;
        flags = pending->flags;
        if (status != AIS_OK) {
            if (!ais_status_is_retryable(status)) {
                ais_mark_rejected_locked(ctx, sensor, provider);
                ais_owned_observation_free(pending);
                memmove(
                    &ctx->pending[0], &ctx->pending[1],
                    (ctx->pending_count - 1u) * sizeof(*ctx->pending));
                ctx->pending_count -= 1u;
            }
            process_status = ais_error_set_entry(
                error, status, ais_status_is_retryable(status),
                provider, sensor, "process", "observation commit failed");
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            *out_processed = processed;
            return process_status;
        }
        committed = *pending;
        memset(pending, 0, sizeof(*pending));
        memmove(
            &ctx->pending[0], &ctx->pending[1],
            (ctx->pending_count - 1u) * sizeof(*ctx->pending));
        ctx->pending_count -= 1u;
        (void)mtx_unlock(&ctx->mutex);
        ais_dispatch_event(
            ctx, AIS_EVENT_ACCEPTED, AIS_OK, sensor_id, provider_id,
            sequence, flags);
        if (percept_generated) {
            ais_dispatch_event(
                ctx, AIS_EVENT_PERCEPT_GENERATED, AIS_OK, sensor_id,
                provider_id, sequence, flags);
        }
        ais_owned_observation_free(&committed);
        processed += 1u;
    }
    *out_processed = processed;
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_schema_register(
    ais_ctx_t *ctx, const ais_schema_config_t *config,
    ais_error_info_t *error)
{
    ais_schema_config_t normalized;
    ais_schema_entry_t *entry;
    uint32_t supported_payloads =
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_BOOL) |
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_INT) |
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_REAL) |
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_VECTOR) |
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_EVENT) |
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT);
    if (!ais_schema_config_view(config, &normalized) ||
        !ais_text_valid(normalized.schema_id, 1u) ||
        normalized.schema_major != AIS_SCHEMA_MAJOR ||
        normalized.schema_minor > AIS_SCHEMA_MINOR ||
        normalized.modality == 0u ||
        normalized.allowed_payload_kinds == 0u ||
        (normalized.allowed_payload_kinds & ~supported_payloads) != 0u ||
        !ais_text_valid(normalized.default_unit, 0u) ||
        !ais_text_valid(normalized.default_reference_frame, 0u)) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "schema_register", "invalid schema configuration");
    }
    if ((normalized.callbacks.validate != NULL ||
         normalized.callbacks.destroy != NULL) &&
        !ais_header_valid(
            &normalized.callbacks.header, sizeof(normalized.callbacks))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "schema_register", "invalid callback header");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "schema_register", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "schema_register", "context is closing");
    }
    if (ais_builtin_find(normalized.schema_id) != NULL ||
        ais_custom_schema_find(ctx, normalized.schema_id) != NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_CONFLICT, 0u, NULL, NULL,
            "schema_register", "schema identifier already exists");
    }
    entry = (ais_schema_entry_t *)calloc(1u, sizeof(*entry));
    if (entry == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "schema_register", "schema allocation failed");
    }
    entry->major = normalized.schema_major;
    entry->minor = normalized.schema_minor;
    entry->modality = normalized.modality;
    entry->allowed_payload_kinds = normalized.allowed_payload_kinds;
    entry->callbacks = normalized.callbacks;
    entry->user_data = normalized.user_data;
    entry->active = 1u;
    if (!ais_owned_string_set(&entry->schema_id, normalized.schema_id) ||
        !ais_owned_string_set(&entry->default_unit, normalized.default_unit) ||
        !ais_owned_string_set(
            &entry->default_reference_frame,
            normalized.default_reference_frame) ||
        !ais_array_grow(
            (void **)&ctx->schemas, &ctx->schema_capacity,
            ctx->schema_count + 1u, sizeof(*ctx->schemas))) {
        entry->callbacks.destroy = NULL;
        ais_schema_entry_free(entry);
        free(entry);
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "schema_register", "schema allocation failed");
    }
    ctx->schemas[ctx->schema_count++] = entry;
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_schema_unregister(
    ais_ctx_t *ctx, ais_string_view_t schema_id, ais_error_info_t *error)
{
    ais_schema_entry_t *removed;
    size_t position = SIZE_MAX;
    if (!ais_text_valid(schema_id, 1u)) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "schema_unregister", "invalid schema identifier");
    }
    if (ais_callback_active(ctx, AIS_CALLBACK_SCHEMA)) {
        return ais_error_set(
            error, AIS_E_BUSY, 1u, NULL, NULL,
            "schema_unregister", "schema callback cannot unregister itself");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "schema_unregister", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "schema_unregister", "context is closing");
    }
    for (size_t index = 0u; index < ctx->schema_count; ++index) {
        if (ctx->schemas[index]->active &&
            ais_string_equal(&ctx->schemas[index]->schema_id, schema_id)) {
            position = index;
            break;
        }
    }
    if (position == SIZE_MAX) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        if (ais_builtin_find(schema_id) != NULL) {
            return ais_error_set(
                error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
                "schema_unregister", "built-in schema cannot be removed");
        }
        return ais_error_set(
            error, AIS_E_SCHEMA_MISMATCH, 0u, NULL, NULL,
            "schema_unregister", "schema is not registered");
    }
    for (size_t index = 0u; index < ctx->sensor_count; ++index) {
        if (ais_string_equal(&ctx->sensors[index].schema, schema_id)) {
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            return ais_error_set(
                error, AIS_E_CONFLICT, 0u, NULL, NULL,
                "schema_unregister", "schema is still used by a sensor");
        }
    }
    removed = ctx->schemas[position];
    removed->active = 0u;
    while (removed->in_flight != 0u) {
        if (cnd_wait(&ctx->condition, &ctx->mutex) != thrd_success) {
            removed->active = 1u;
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            return ais_error_set(
                error, AIS_E_INTERNAL, 0u, NULL, NULL,
                "schema_unregister", "schema callback wait failed");
        }
    }
    memmove(
        &ctx->schemas[position], &ctx->schemas[position + 1u],
        (ctx->schema_count - position - 1u) * sizeof(*ctx->schemas));
    ctx->schema_count -= 1u;
    (void)mtx_unlock(&ctx->mutex);
    ais_callback_guard_t callback_guard;
    ais_callback_enter(ctx, AIS_CALLBACK_SCHEMA, &callback_guard);
    ais_schema_entry_free(removed);
    free(removed);
    ais_callback_leave(&callback_guard);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_event_subscribe(
    ais_ctx_t *ctx, uint32_t event_mask, ais_event_fn callback,
    void *user_data, void (*destroy)(void *user_data),
    uint64_t *out_subscription, ais_error_info_t *error)
{
    ais_event_entry_t *entry;
    if (out_subscription == NULL || event_mask == 0u ||
        (event_mask & ~ais_known_event_mask()) != 0u) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "event_subscribe", "invalid event subscription");
    }
    *out_subscription = 0u;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "event_subscribe", "context is unavailable");
    }
    entry = (ais_event_entry_t *)calloc(1u, sizeof(*entry));
    if (entry == NULL) {
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "event_subscribe", "subscription allocation failed");
    }
    entry->mask = event_mask;
    entry->callback = callback;
    entry->user_data = user_data;
    entry->destroy = destroy;
    entry->active = 1u;
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        free(entry);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "event_subscribe", "context is closing");
    }
    if (ctx->next_subscription_id == UINT64_MAX) {
        (void)mtx_unlock(&ctx->mutex);
        free(entry);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_BUFFER_TOO_SMALL, 1u, NULL, NULL,
            "event_subscribe", "subscription identifier space exhausted");
    }
    entry->id = ctx->next_subscription_id++;
    if (!ais_array_grow(
            (void **)&ctx->events, &ctx->event_capacity,
            ctx->event_count + 1u, sizeof(*ctx->events))) {
        (void)mtx_unlock(&ctx->mutex);
        free(entry);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "event_subscribe", "subscription table allocation failed");
    }
    ctx->events[ctx->event_count++] = entry;
    *out_subscription = entry->id;
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_event_unsubscribe(
    ais_ctx_t *ctx, uint64_t subscription, ais_error_info_t *error)
{
    ais_event_entry_t *removed = NULL;
    size_t position = SIZE_MAX;
    if (subscription == 0u) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "event_unsubscribe", "invalid subscription");
    }
    if (ais_callback_active(ctx, AIS_CALLBACK_EVENT)) {
        return ais_error_set(
            error, AIS_E_BUSY, 1u, NULL, NULL,
            "event_unsubscribe", "event callback cannot unsubscribe");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "event_unsubscribe", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "event_unsubscribe", "context is closing");
    }
    for (size_t index = 0u; index < ctx->event_count; ++index) {
        if (ctx->events[index] != NULL &&
            ctx->events[index]->id == subscription &&
            ctx->events[index]->active) {
            position = index;
            break;
        }
    }
    if (position == SIZE_MAX) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "event_unsubscribe", "subscription is not active");
    }
    removed = ctx->events[position];
    removed->active = 0u;
    while (removed->in_flight != 0u) {
        if (cnd_wait(&ctx->condition, &ctx->mutex) != thrd_success) {
            removed->active = 1u;
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            return ais_error_set(
                error, AIS_E_INTERNAL, 0u, NULL, NULL,
                "event_unsubscribe", "event callback wait failed");
        }
    }
    memmove(
        &ctx->events[position], &ctx->events[position + 1u],
        (ctx->event_count - position - 1u) * sizeof(*ctx->events));
    ctx->event_count -= 1u;
    (void)mtx_unlock(&ctx->mutex);
    if (removed->destroy != NULL) {
        ais_callback_guard_t callback_guard;
        ais_callback_enter(ctx, AIS_CALLBACK_EVENT, &callback_guard);
        removed->destroy(removed->user_data);
        ais_callback_leave(&callback_guard);
    }
    free(removed);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

static void ais_provider_entry_free(ais_provider_entry_t *provider)
{
    if (provider == NULL) {
        return;
    }
    free(provider->emit_context);
    provider->emit_context = NULL;
    ais_owned_string_free(&provider->key);
    ais_owned_string_free(&provider->version);
    memset(provider, 0, sizeof(*provider));
}

static void ais_sensor_entry_free(ais_sensor_entry_t *sensor)
{
    if (sensor == NULL) {
        return;
    }
    ais_owned_string_free(&sensor->key);
    ais_owned_string_free(&sensor->schema);
    ais_owned_string_free(&sensor->unit_id);
    ais_owned_string_free(&sensor->reference_frame_id);
    ais_owned_string_free(&sensor->sensor_type);
    ais_owned_string_free(&sensor->name);
    ais_owned_string_free(&sensor->description);
    ais_owned_string_free(&sensor->session_id);
    ais_owned_string_free(&sensor->source_type);
    ais_owned_observation_free(&sensor->latest);
    free(sensor->sequence_history);
    memset(sensor, 0, sizeof(*sensor));
}

static void ais_schema_entry_free(ais_schema_entry_t *schema)
{
    if (schema == NULL) {
        return;
    }
    if (schema->callbacks.destroy != NULL) {
        schema->callbacks.destroy(schema->user_data);
    }
    ais_owned_string_free(&schema->schema_id);
    ais_owned_string_free(&schema->default_unit);
    ais_owned_string_free(&schema->default_reference_frame);
    memset(schema, 0, sizeof(*schema));
}

static int ais_ctx_enter(ais_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->mutex_ready) {
        return 0;
    }
    if (mtx_lock(&ctx->mutex) != thrd_success) {
        return 0;
    }
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        return 0;
    }
    ctx->active_calls += 1u;
    (void)mtx_unlock(&ctx->mutex);
    return 1;
}

static void ais_ctx_leave(ais_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->mutex_ready) {
        return;
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->active_calls > 0u) {
        ctx->active_calls -= 1u;
    }
    if (ctx->active_calls == 0u) {
        (void)cnd_broadcast(&ctx->condition);
    }
    (void)mtx_unlock(&ctx->mutex);
}

static void ais_ctx_destroy_storage(ais_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    for (size_t index = 0u; index < ctx->provider_count; ++index) {
        ais_provider_entry_free(&ctx->providers[index]);
    }
    for (size_t index = 0u; index < ctx->sensor_count; ++index) {
        ais_sensor_entry_free(&ctx->sensors[index]);
    }
    for (size_t index = 0u; index < ctx->schema_count; ++index) {
        ais_schema_entry_t *schema = ctx->schemas[index];
        ais_callback_guard_t callback_guard;
        ais_callback_enter(ctx, AIS_CALLBACK_SCHEMA, &callback_guard);
        ais_schema_entry_free(schema);
        free(schema);
        ais_callback_leave(&callback_guard);
    }
    for (size_t index = 0u; index < ctx->recent_count; ++index) {
        ais_owned_observation_free(&ctx->recent[index]);
    }
    for (size_t index = 0u; index < ctx->percept_count; ++index) {
        ais_owned_percept_free(&ctx->percepts[index]);
    }
    for (size_t index = 0u; index < ctx->pending_count; ++index) {
        ais_owned_observation_free(&ctx->pending[index]);
    }
    free(ctx->providers);
    free(ctx->sensors);
    free(ctx->schemas);
    free(ctx->recent);
    free(ctx->percepts);
    free(ctx->pending);
    for (size_t index = 0u; index < ctx->event_count; ++index) {
        ais_event_entry_t *event = ctx->events[index];
        if (event != NULL) {
            if (event->destroy != NULL) {
                ais_callback_guard_t callback_guard;
                ais_callback_enter(ctx, AIS_CALLBACK_EVENT, &callback_guard);
                event->destroy(event->user_data);
                ais_callback_leave(&callback_guard);
            }
            free(event);
        }
    }
    free(ctx->events);
    ais_owned_string_free(&ctx->default_session);
    ais_owned_string_free(&ctx->last_result_id);
}

static int ais_ctx_add_implicit_provider(ais_ctx_t *ctx)
{
    ais_provider_entry_t *provider;
    if (!ais_array_grow(
            (void **)&ctx->providers, &ctx->provider_capacity,
            ctx->provider_count + 1u, sizeof(*ctx->providers))) {
        return 0;
    }
    provider = &ctx->providers[ctx->provider_count++];
    memset(provider, 0, sizeof(*provider));
    provider->id = 1u;
    provider->implicit = 1u;
    provider->started = 1u;
    provider->availability = AIS_AVAIL_ACTIVE;
    provider->last_error = AIS_OK;
    provider->emit_context = (ais_provider_emit_context_t *)calloc(
        1u, sizeof(*provider->emit_context));
    if (provider->emit_context == NULL) {
        ais_provider_entry_free(provider);
        ctx->provider_count -= 1u;
        return 0;
    }
    provider->emit_context->ctx = ctx;
    provider->emit_context->provider_id = provider->id;
    atomic_init(&provider->emit_context->active, false);
    if (!ais_owned_string_set_literal(&provider->key, "application") ||
        !ais_owned_string_set_literal(&provider->version, "1.0")) {
        ais_provider_entry_free(provider);
        ctx->provider_count -= 1u;
        return 0;
    }
    return 1;
}

AIS_API ais_status_t ais_ctx_config_init(ais_ctx_config_t *config)
{
    if (config == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    memset(config, 0, sizeof(*config));
    ais_header_init(&config->header, sizeof(*config));
    config->max_recent_observations = (uint32_t)AIS_DEFAULT_MAX_RECENT;
    config->processing_mode = AIS_PROCESS_ON_PUBLISH;
    config->stale_after_ns = AIS_DEFAULT_STALE_NS;
    config->max_payload_bytes = (uint64_t)AIS_DEFAULT_MAX_PAYLOAD;
    return AIS_OK;
}

AIS_API ais_status_t ais_sensor_config_init(ais_sensor_config_t *config)
{
    if (config == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    memset(config, 0, sizeof(*config));
    ais_header_init(&config->header, sizeof(*config));
    return AIS_OK;
}

AIS_API ais_status_t ais_sensor_status_init(ais_sensor_status_t *status)
{
    if (status == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    memset(status, 0, sizeof(*status));
    ais_header_init(&status->header, sizeof(*status));
    return AIS_OK;
}

AIS_API ais_status_t ais_provider_config_init(ais_provider_config_t *config)
{
    if (config == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    memset(config, 0, sizeof(*config));
    ais_header_init(&config->header, sizeof(*config));
    ais_header_init(&config->vtable.header, sizeof(config->vtable));
    return AIS_OK;
}

AIS_API ais_status_t ais_provider_status_init(
    ais_provider_status_t *status)
{
    if (status == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    memset(status, 0, sizeof(*status));
    ais_header_init(&status->header, sizeof(*status));
    status->availability = AIS_AVAIL_DISCOVERED;
    return AIS_OK;
}

static ais_status_t ais_validate_ctx_config(
    const ais_ctx_config_t *config, ais_ctx_config_t *normalized,
    ais_error_info_t *error)
{
    ais_ctx_config_t defaults;
    if (normalized == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "ctx_create", "missing normalized configuration");
    }
    (void)ais_ctx_config_init(&defaults);
    if (config == NULL) {
        *normalized = defaults;
        return AIS_OK;
    }
    if (!ais_header_valid(
            &config->header, sizeof(ais_struct_header_t))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "ctx_create", "invalid configuration header");
    }
    *normalized = defaults;
    size_t available = config->header.size;
#define AIS_COPY_CONFIG_FIELD(field) \
    if (available >= offsetof(ais_ctx_config_t, field) + sizeof(config->field)) \
        normalized->field = config->field
    AIS_COPY_CONFIG_FIELD(max_recent_observations);
    AIS_COPY_CONFIG_FIELD(processing_mode);
    AIS_COPY_CONFIG_FIELD(stale_after_ns);
    AIS_COPY_CONFIG_FIELD(max_payload_bytes);
    AIS_COPY_CONFIG_FIELD(flags);
#undef AIS_COPY_CONFIG_FIELD
    if (normalized->max_recent_observations == 0u) {
        normalized->max_recent_observations =
            (uint32_t)AIS_DEFAULT_MAX_RECENT;
    }
    if (normalized->processing_mode != AIS_PROCESS_ON_PUBLISH &&
        normalized->processing_mode != AIS_PROCESS_MANUAL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "ctx_create", "unsupported processing mode");
    }
#if SIZE_MAX < UINT32_MAX
    if (normalized->max_recent_observations >
            SIZE_MAX / sizeof(ais_owned_observation_t)) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "ctx_create", "recent observation limit overflows");
    }
#endif
    if (normalized->max_payload_bytes == 0u) {
        normalized->max_payload_bytes = (uint64_t)AIS_DEFAULT_MAX_PAYLOAD;
    }
    if (normalized->max_payload_bytes > SIZE_MAX) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "ctx_create", "payload limit overflows");
    }
    if (normalized->stale_after_ns == 0u) {
        normalized->stale_after_ns = AIS_DEFAULT_STALE_NS;
    }
    return AIS_OK;
}

AIS_API ais_status_t ais_ctx_create(
    const ais_ctx_config_t *config, ais_ctx_t **out_ctx,
    ais_error_info_t *error)
{
    ais_ctx_config_t normalized;
    ais_ctx_t *ctx;
    ais_status_t status;
    if (out_ctx == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "ctx_create", "missing output context");
    }
    *out_ctx = NULL;
    status = ais_validate_ctx_config(config, &normalized, error);
    if (status != AIS_OK) {
        return status;
    }
    ctx = (ais_ctx_t *)calloc(1u, sizeof(*ctx));
    if (ctx == NULL) {
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "ctx_create", "context allocation failed");
    }
    if (mtx_init(&ctx->mutex, mtx_plain) != thrd_success) {
        free(ctx);
        return ais_error_set(
            error, AIS_E_INTERNAL, 0u, NULL, NULL,
            "ctx_create", "synchronization initialization failed");
    }
    ctx->mutex_ready = 1u;
    if (cnd_init(&ctx->condition) != thrd_success) {
        mtx_destroy(&ctx->mutex);
        free(ctx);
        return ais_error_set(
            error, AIS_E_INTERNAL, 0u, NULL, NULL,
            "ctx_create", "synchronization initialization failed");
    }
    ctx->lifetime = ais_lifetime_create();
    if (ctx->lifetime == NULL ||
        !ais_owned_string_set_literal(&ctx->default_session, "ais-session-1") ||
        !ais_ctx_add_implicit_provider(ctx)) {
        if (ctx->lifetime != NULL) {
            atomic_store_explicit(
                &ctx->lifetime->closed, true, memory_order_release);
            ais_lifetime_release(ctx->lifetime);
        }
        ais_ctx_destroy_storage(ctx);
        cnd_destroy(&ctx->condition);
        mtx_destroy(&ctx->mutex);
        free(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "ctx_create", "context initialization failed");
    }
    ctx->max_recent = normalized.max_recent_observations;
    ctx->max_payload = (size_t)normalized.max_payload_bytes;
    ctx->dedup_history_limit = AIS_DEFAULT_DEDUP_HISTORY;
    ctx->reject_duplicates = 1u;
    ctx->flag_out_of_order = 1u;
    ctx->retain_dropped_predecessor_flag = 0u;
    ctx->processing_mode = normalized.processing_mode;
    ctx->flags = normalized.flags;
    ctx->stale_after_ns = normalized.stale_after_ns;
    ctx->next_sensor_id = 1u;
    ctx->next_provider_id = 2u;
    ctx->next_observation_id = 1u;
    ctx->next_subscription_id = 1u;
    *out_ctx = ctx;
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_ctx_create_default(
    ais_ctx_t **out_ctx, ais_error_info_t *error)
{
    return ais_ctx_create(NULL, out_ctx, error);
}

AIS_API ais_status_t ais_ctx_close(
    ais_ctx_t **inout_ctx, ais_error_info_t *error)
{
    ais_ctx_t *ctx;
    if (inout_ctx == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "ctx_close", "missing context handle address");
    }
    if (*inout_ctx == NULL) {
        ais_error_clear(error);
        return AIS_OK;
    }
    ctx = *inout_ctx;
    if (ais_callback_active(ctx, AIS_CALLBACK_NONE)) {
        return ais_error_set(
            error, AIS_E_BUSY, 1u, NULL, NULL,
            "ctx_close", "close from a callback is not allowed");
    }
    if (!ctx->mutex_ready ||
        mtx_lock(&ctx->mutex) != thrd_success) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "ctx_close", "invalid context synchronization");
    }
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        return ais_error_set(
            error, AIS_E_BUSY, 1u, NULL, NULL,
            "ctx_close", "context is already closing");
    }
    ctx->closing = 1u;
    atomic_store_explicit(
        &ctx->lifetime->closed, true, memory_order_release);
    (void)mtx_unlock(&ctx->mutex);

    for (size_t index = 0u;; ++index) {
        ais_provider_stop_fn stop = NULL;
        void *user_data = NULL;
        (void)mtx_lock(&ctx->mutex);
        if (index >= ctx->provider_count) {
            (void)mtx_unlock(&ctx->mutex);
            break;
        }
        ais_provider_entry_t *provider = &ctx->providers[index];
        if (provider->started && !provider->lifecycle_in_progress) {
            stop = provider->vtable.stop;
            user_data = provider->user_data;
            provider->started = 0u;
            provider->availability = AIS_AVAIL_STOPPING;
        }
        (void)mtx_unlock(&ctx->mutex);
        if (stop != NULL) {
            ais_callback_guard_t callback_guard;
            ais_callback_enter(ctx, AIS_CALLBACK_PROVIDER, &callback_guard);
            (void)stop(user_data);
            ais_callback_leave(&callback_guard);
        }
    }

    (void)mtx_lock(&ctx->mutex);
    while (ctx->active_calls != 0u) {
        if (cnd_wait(&ctx->condition, &ctx->mutex) != thrd_success) {
            break;
        }
    }
    (void)mtx_unlock(&ctx->mutex);

    for (size_t index = 0u;; ++index) {
        ais_provider_stop_fn stop = NULL;
        void *user_data = NULL;
        (void)mtx_lock(&ctx->mutex);
        if (index >= ctx->provider_count) {
            (void)mtx_unlock(&ctx->mutex);
            break;
        }
        ais_provider_entry_t *provider = &ctx->providers[index];
        if (provider->started) {
            stop = provider->vtable.stop;
            user_data = provider->user_data;
            provider->started = 0u;
            provider->availability = AIS_AVAIL_STOPPING;
        }
        (void)mtx_unlock(&ctx->mutex);
        if (stop != NULL) {
            ais_callback_guard_t callback_guard;
            ais_callback_enter(ctx, AIS_CALLBACK_PROVIDER, &callback_guard);
            (void)stop(user_data);
            ais_callback_leave(&callback_guard);
        }
    }

    for (size_t index = 0u; index < ctx->provider_count; ++index) {
        ais_provider_entry_t *provider = &ctx->providers[index];
        if (!provider->implicit && provider->vtable.destroy != NULL) {
            ais_callback_guard_t callback_guard;
            ais_callback_enter(ctx, AIS_CALLBACK_PROVIDER, &callback_guard);
            provider->vtable.destroy(provider->user_data);
            ais_callback_leave(&callback_guard);
        }
    }

    ais_ctx_destroy_storage(ctx);
    ais_lifetime_release(ctx->lifetime);
    cnd_destroy(&ctx->condition);
    mtx_destroy(&ctx->mutex);
    ctx->mutex_ready = 0u;
    free(ctx);
    *inout_ctx = NULL;
    ais_error_clear(error);
    return AIS_OK;
}

static int ais_schema_resolve(
    ais_ctx_t *ctx, ais_string_view_t schema_id,
    const ais_builtin_schema_t **builtin, ais_schema_entry_t **custom)
{
    if (builtin != NULL) {
        *builtin = NULL;
    }
    if (custom != NULL) {
        *custom = NULL;
    }
    if (!ais_text_valid(schema_id, 1u)) {
        return 0;
    }
    if (builtin != NULL) {
        *builtin = ais_builtin_find(schema_id);
        if (*builtin != NULL) {
            return 1;
        }
    }
    if (custom != NULL) {
        *custom = ais_custom_schema_find(ctx, schema_id);
        return *custom != NULL;
    }
    return 0;
}

static int ais_schema_defaults(
    ais_ctx_t *ctx, ais_string_view_t schema_id, uint32_t *modality,
    uint32_t *payload_mask, uint32_t *unit_code, ais_string_view_t *unit_id,
    uint32_t *reference_code, ais_string_view_t *reference_id,
    uint32_t *acquisition, uint32_t *capabilities)
{
    const ais_builtin_schema_t *builtin = NULL;
    ais_schema_entry_t *custom = NULL;
    if (!ais_schema_resolve(ctx, schema_id, &builtin, &custom)) {
        return 0;
    }
    if (builtin != NULL) {
        if (modality != NULL) *modality = builtin->modality;
        if (payload_mask != NULL) *payload_mask = builtin->payload_mask;
        if (unit_code != NULL) *unit_code = builtin->unit_code;
        if (unit_id != NULL) {
            *unit_id = (ais_string_view_t){
                builtin->unit_id, strlen(builtin->unit_id)
            };
        }
        if (reference_code != NULL) {
            *reference_code = builtin->reference_frame_code;
        }
        if (reference_id != NULL) {
            *reference_id = (ais_string_view_t){
                builtin->reference_frame_id,
                strlen(builtin->reference_frame_id)
            };
        }
        if (acquisition != NULL) *acquisition = builtin->acquisition;
        if (capabilities != NULL) *capabilities = builtin->capabilities;
        return 1;
    }
    if (modality != NULL) *modality = custom->modality;
    if (payload_mask != NULL) *payload_mask = custom->allowed_payload_kinds;
    if (unit_code != NULL) *unit_code = ais_unit_code(
        ais_owned_string_view(&custom->default_unit));
    if (unit_id != NULL) {
        *unit_id = ais_owned_string_view(&custom->default_unit);
    }
    if (reference_code != NULL) *reference_code = ais_reference_code(
        ais_owned_string_view(&custom->default_reference_frame));
    if (reference_id != NULL) {
        *reference_id = ais_owned_string_view(
            &custom->default_reference_frame);
    }
    if (acquisition != NULL) *acquisition = AIS_ACQUISITION_POINT;
    if (capabilities != NULL) *capabilities = AIS_CAP_READ;
    return 1;
}

static int ais_config_view(
    const ais_sensor_config_t *source, ais_sensor_config_t *target)
{
    size_t available;
    if (source == NULL || target == NULL ||
        !ais_header_valid(
            &source->header, sizeof(ais_struct_header_t))) {
        return 0;
    }
    (void)ais_sensor_config_init(target);
    available = source->header.size;
#define AIS_COPY_SENSOR_FIELD(field) \
    if (available >= offsetof(ais_sensor_config_t, field) + sizeof(source->field)) \
        target->field = source->field
    AIS_COPY_SENSOR_FIELD(key);
    AIS_COPY_SENSOR_FIELD(schema);
    AIS_COPY_SENSOR_FIELD(unit);
    AIS_COPY_SENSOR_FIELD(reference_frame);
    AIS_COPY_SENSOR_FIELD(sensor_type);
    AIS_COPY_SENSOR_FIELD(name);
    AIS_COPY_SENSOR_FIELD(description);
    AIS_COPY_SENSOR_FIELD(session_id);
    AIS_COPY_SENSOR_FIELD(source_type);
    AIS_COPY_SENSOR_FIELD(modality);
    AIS_COPY_SENSOR_FIELD(acquisition_mode);
    AIS_COPY_SENSOR_FIELD(capabilities);
    AIS_COPY_SENSOR_FIELD(permission_required);
    AIS_COPY_SENSOR_FIELD(max_rate_hz);
    AIS_COPY_SENSOR_FIELD(provider_id);
#undef AIS_COPY_SENSOR_FIELD
    return 1;
}

AIS_API ais_status_t ais_sensor_register(
    ais_ctx_t *ctx, const ais_sensor_config_t *config,
    ais_sensor_id_t *out_sensor, ais_error_info_t *error)
{
    ais_sensor_config_t normalized;
    ais_sensor_entry_t entry;
    ais_provider_entry_t *provider;
    const ais_builtin_schema_t *builtin = NULL;
    ais_schema_entry_t *custom = NULL;
    ais_string_view_t session;
    ais_string_view_t unit;
    ais_string_view_t reference;
    ais_string_view_t source;
    uint32_t default_modality = 0u;
    uint32_t default_payload = 0u;
    uint32_t default_unit = 0u;
    uint32_t default_reference = 0u;
    uint32_t default_acquisition = 0u;
    uint32_t default_capabilities = 0u;
    ais_string_view_t default_unit_id = { NULL, 0u };
    ais_string_view_t default_reference_id = { NULL, 0u };
    if (out_sensor == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "sensor_register", "missing output sensor");
    }
    *out_sensor = 0u;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "sensor_register", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "sensor_register", "context is closing");
    }
    if (!ais_config_view(config, &normalized) ||
        !ais_text_valid(normalized.key, 1u) ||
        !ais_text_valid(normalized.schema, 1u)) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "sensor_register", "invalid sensor configuration");
    }
    (void)ais_schema_resolve(
        ctx, normalized.schema, &builtin, &custom);
    if (builtin == NULL && custom == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_SCHEMA_MISMATCH, 0u, NULL, NULL,
            "sensor_register", "schema is not registered");
    }
    if (!ais_schema_defaults(
            ctx, normalized.schema, &default_modality, &default_payload,
            &default_unit, &default_unit_id, &default_reference,
            &default_reference_id, &default_acquisition,
            &default_capabilities)) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_SCHEMA_MISMATCH, 0u, NULL, NULL,
            "sensor_register", "schema metadata unavailable");
    }
    (void)default_payload;
    provider = ais_provider_find(
        ctx, normalized.provider_id == 0u ? 1u : normalized.provider_id);
    if (provider == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, NULL,
            "sensor_register", "provider is not registered");
    }
    session = normalized.session_id.size > 0u ?
        normalized.session_id : ais_owned_string_view(&ctx->default_session);
    unit = normalized.unit.size > 0u ? normalized.unit : default_unit_id;
    reference = normalized.reference_frame.size > 0u ?
        normalized.reference_frame : default_reference_id;
    source = normalized.source_type.size > 0u ?
        normalized.source_type : ais_owned_string_view(&provider->key);
    if (!ais_text_valid(session, 1u) || !ais_text_valid(unit, 0u) ||
        !ais_text_valid(reference, 0u) || !ais_text_valid(source, 1u) ||
        normalized.permission_required > 1u ||
        !ais_double_valid(normalized.max_rate_hz) ||
        normalized.max_rate_hz < 0.0 ||
        (normalized.acquisition_mode != 0u &&
         (normalized.acquisition_mode < AIS_ACQUISITION_POINT ||
          normalized.acquisition_mode > AIS_ACQUISITION_STREAM))) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_INVALID_ARGUMENT, 0u, provider, NULL,
            "sensor_register", "invalid sensor field");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    if ((normalized.modality != 0u &&
         normalized.modality != default_modality) ||
        (normalized.acquisition_mode != 0u &&
         normalized.acquisition_mode != default_acquisition) ||
        (builtin != NULL &&
         (ais_unit_code(unit) != default_unit ||
          unit.size != default_unit_id.size ||
          (unit.size > 0u &&
           memcmp(unit.data, default_unit_id.data, unit.size) != 0) ||
          ais_reference_code(reference) != default_reference ||
          reference.size != default_reference_id.size ||
          (reference.size > 0u &&
           memcmp(reference.data, default_reference_id.data,
                  reference.size) != 0))) ||
        (custom != NULL && default_unit_id.size > 0u &&
         (unit.size != default_unit_id.size ||
          memcmp(unit.data, default_unit_id.data, unit.size) != 0)) ||
        (custom != NULL && default_reference_id.size > 0u &&
         (reference.size != default_reference_id.size ||
          memcmp(reference.data, default_reference_id.data,
                 reference.size) != 0))) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_SCHEMA_MISMATCH, 0u, provider, NULL,
            "sensor_register", "sensor contract differs from schema");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    for (size_t index = 0u; index < ctx->sensor_count; ++index) {
        ais_sensor_entry_t *existing = &ctx->sensors[index];
        if (existing->provider_id == provider->id &&
            ais_string_equal(&existing->key, normalized.key) &&
            ais_string_equal(&existing->session_id, session)) {
            ais_status_t failure = ais_error_set_entry(
                error, AIS_E_CONFLICT, 0u, provider, existing,
                "sensor_register", "sensor identity collision");
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            return failure;
        }
    }
    if (ctx->next_sensor_id == UINT64_MAX) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_BUFFER_TOO_SMALL, 1u, NULL, NULL,
            "sensor_register", "sensor identifier space exhausted");
    }
    memset(&entry, 0, sizeof(entry));
    entry.id = ctx->next_sensor_id++;
    entry.provider_id = provider->id;
    entry.modality = normalized.modality == 0u ?
        default_modality : normalized.modality;
    entry.acquisition_mode = normalized.acquisition_mode == 0u ?
        default_acquisition : normalized.acquisition_mode;
    entry.capabilities = normalized.capabilities == 0u ?
        default_capabilities : normalized.capabilities;
    entry.permission_required = normalized.permission_required;
    entry.max_rate_hz = normalized.max_rate_hz;
    entry.unit_code = normalized.unit.size == 0u ?
        default_unit : ais_unit_code(unit);
    entry.reference_frame_code = normalized.reference_frame.size == 0u ?
        default_reference : ais_reference_code(reference);
    entry.availability = provider->availability;
    entry.last_error = provider->last_error;
    entry.next_sequence = 1u;
    if (!ais_owned_string_set(&entry.key, normalized.key) ||
        !ais_owned_string_set(&entry.schema, normalized.schema) ||
        !ais_owned_string_set(&entry.unit_id, unit) ||
        !ais_owned_string_set(&entry.reference_frame_id, reference) ||
        !ais_owned_string_set(
            &entry.sensor_type,
            normalized.sensor_type.size > 0u ?
                normalized.sensor_type : normalized.schema) ||
        !ais_owned_string_set(
            &entry.name,
            normalized.name.size > 0u ? normalized.name : normalized.key) ||
        !ais_owned_string_set(&entry.description, normalized.description) ||
        !ais_owned_string_set(&entry.session_id, session) ||
        !ais_owned_string_set(&entry.source_type, source) ||
        !ais_array_grow(
            (void **)&ctx->sensors, &ctx->sensor_capacity,
            ctx->sensor_count + 1u, sizeof(*ctx->sensors))) {
        ais_status_t failure;
        ais_sensor_entry_free(&entry);
        failure = ais_error_set_entry(
            error, AIS_E_OUT_OF_MEMORY, 1u, provider, NULL,
            "sensor_register", "sensor allocation failed");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    ctx->sensors[ctx->sensor_count++] = entry;
    *out_sensor = entry.id;
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_sensor_register_simple(
    ais_ctx_t *ctx, ais_string_view_t key, ais_string_view_t schema,
    ais_string_view_t unit, ais_sensor_id_t *out_sensor,
    ais_error_info_t *error)
{
    ais_sensor_config_t config;
    (void)ais_sensor_config_init(&config);
    config.key = key;
    config.schema = schema;
    config.unit = unit;
    return ais_sensor_register(ctx, &config, out_sensor, error);
}

static int ais_provider_config_view(
    const ais_provider_config_t *source, ais_provider_config_t *target)
{
    size_t available;
    if (source == NULL || target == NULL ||
        !ais_header_valid(
            &source->header, sizeof(ais_struct_header_t))) {
        return 0;
    }
    (void)ais_provider_config_init(target);
    available = source->header.size;
#define AIS_COPY_PROVIDER_FIELD(field) \
    if (available >= offsetof(ais_provider_config_t, field) + sizeof(source->field)) \
        target->field = source->field
    AIS_COPY_PROVIDER_FIELD(key);
    AIS_COPY_PROVIDER_FIELD(version);
    AIS_COPY_PROVIDER_FIELD(capabilities);
    AIS_COPY_PROVIDER_FIELD(vtable);
    AIS_COPY_PROVIDER_FIELD(user_data);
#undef AIS_COPY_PROVIDER_FIELD
    return ais_header_valid(
        &target->vtable.header, sizeof(ais_struct_header_t));
}

static int ais_provider_key_exists(
    ais_ctx_t *ctx, ais_string_view_t key)
{
    for (size_t index = 0u; index < ctx->provider_count; ++index) {
        if (ais_string_equal(&ctx->providers[index].key, key)) {
            return 1;
        }
    }
    return 0;
}

AIS_API ais_status_t ais_provider_register(
    ais_ctx_t *ctx, const ais_provider_config_t *config,
    ais_provider_id_t *out_provider, ais_error_info_t *error)
{
    ais_provider_config_t normalized;
    ais_provider_entry_t entry;
    if (out_provider == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "provider_register", "missing output provider");
    }
    *out_provider = 0u;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_register", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_register", "context is closing");
    }
    if (!ais_provider_config_view(config, &normalized) ||
        !ais_text_valid(normalized.key, 1u) ||
        !ais_text_valid(normalized.version, 0u) ||
        ais_provider_key_exists(ctx, normalized.key)) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "provider_register", "invalid or duplicate provider");
    }
    if (ctx->next_provider_id == UINT64_MAX) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_BUFFER_TOO_SMALL, 1u, NULL, NULL,
            "provider_register", "provider identifier space exhausted");
    }
    memset(&entry, 0, sizeof(entry));
    entry.id = ctx->next_provider_id++;
    entry.capabilities = normalized.capabilities;
    entry.vtable = normalized.vtable;
    entry.user_data = normalized.user_data;
    entry.availability = AIS_AVAIL_DISCOVERED;
    entry.last_error = AIS_OK;
    entry.emit_context = (ais_provider_emit_context_t *)calloc(
        1u, sizeof(*entry.emit_context));
    if (entry.emit_context == NULL) {
        ais_provider_entry_free(&entry);
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "provider_register", "provider allocation failed");
    }
    entry.emit_context->ctx = ctx;
    entry.emit_context->provider_id = entry.id;
    atomic_init(&entry.emit_context->active, false);
    if (!ais_owned_string_set(&entry.key, normalized.key) ||
        !ais_owned_string_set(&entry.version, normalized.version) ||
        !ais_array_grow(
            (void **)&ctx->providers, &ctx->provider_capacity,
            ctx->provider_count + 1u, sizeof(*ctx->providers))) {
        ais_provider_entry_free(&entry);
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "provider_register", "provider allocation failed");
    }
    ctx->providers[ctx->provider_count++] = entry;
    *out_provider = entry.id;
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_provider_start(
    ais_ctx_t *ctx, ais_provider_id_t provider_id, ais_error_info_t *error)
{
    ais_provider_entry_t *provider;
    ais_provider_start_fn start;
    void *user_data;
    ais_status_t status = AIS_OK;
    ais_callback_guard_t callback_guard;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_start", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_start", "context is closing");
    }
    provider = ais_provider_find(ctx, provider_id);
    if (provider == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, NULL,
            "provider_start", "provider is not registered");
    }
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_start", "context is closing");
    }
    if (provider->lifecycle_in_progress || provider->poll_in_progress) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_BUSY, 1u, NULL, NULL,
            "provider_start", "provider lifecycle is busy");
    }
    if (provider->started) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        ais_error_clear(error);
        return AIS_OK;
    }
    provider->availability = AIS_AVAIL_INITIALIZING;
    provider->lifecycle_in_progress = 1u;
    start = provider->vtable.start;
    user_data = provider->user_data;
    (void)mtx_unlock(&ctx->mutex);
    if (start != NULL) {
        ais_callback_enter(ctx, AIS_CALLBACK_PROVIDER, &callback_guard);
        status = start(user_data);
        ais_callback_leave(&callback_guard);
    }
    (void)mtx_lock(&ctx->mutex);
    provider = ais_provider_find(ctx, provider_id);
    if (provider != NULL) {
        provider->lifecycle_in_progress = 0u;
        provider->last_error = status;
        provider->started = status == AIS_OK ? 1u : 0u;
        provider->availability = status == AIS_OK ?
            AIS_AVAIL_ACTIVE : AIS_AVAIL_ERROR;
        for (size_t index = 0u; index < ctx->sensor_count; ++index) {
            if (ctx->sensors[index].provider_id == provider_id) {
                ctx->sensors[index].availability = provider->availability;
                ctx->sensors[index].last_error = status;
            }
        }
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_dispatch_event(
        ctx, AIS_EVENT_PROVIDER_AVAILABILITY, status, 0u, provider_id,
        0u, status == AIS_OK ? AIS_AVAIL_ACTIVE : AIS_AVAIL_ERROR);
    ais_ctx_leave(ctx);
    if (status != AIS_OK) {
        return ais_error_set(
            error, status, ais_status_is_retryable(status),
            NULL, NULL, "provider_start", ais_status_message(status));
    }
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_provider_stop(
    ais_ctx_t *ctx, ais_provider_id_t provider_id, ais_error_info_t *error)
{
    ais_provider_entry_t *provider;
    ais_provider_stop_fn stop;
    void *user_data;
    ais_status_t status = AIS_OK;
    ais_callback_guard_t callback_guard;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_stop", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    provider = ais_provider_find(ctx, provider_id);
    if (provider == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, NULL,
            "provider_stop", "provider is not registered");
    }
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_stop", "context is closing");
    }
    if (provider->lifecycle_in_progress || provider->poll_in_progress) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_BUSY, 1u, NULL, NULL,
            "provider_stop", "provider lifecycle is busy");
    }
    if (!provider->started) {
        provider->availability = AIS_AVAIL_STOPPED;
        for (size_t index = 0u; index < ctx->sensor_count; ++index) {
            if (ctx->sensors[index].provider_id == provider_id) {
                ctx->sensors[index].availability = AIS_AVAIL_STOPPED;
                ctx->sensors[index].last_error = AIS_OK;
            }
        }
        (void)mtx_unlock(&ctx->mutex);
        ais_dispatch_event(
            ctx, AIS_EVENT_PROVIDER_AVAILABILITY, AIS_OK, 0u, provider_id,
            0u, AIS_AVAIL_STOPPED);
        ais_ctx_leave(ctx);
        ais_error_clear(error);
        return AIS_OK;
    }
    provider->availability = AIS_AVAIL_STOPPING;
    provider->lifecycle_in_progress = 1u;
    stop = provider->vtable.stop;
    user_data = provider->user_data;
    (void)mtx_unlock(&ctx->mutex);
    if (stop != NULL) {
        ais_callback_enter(ctx, AIS_CALLBACK_PROVIDER, &callback_guard);
        status = stop(user_data);
        ais_callback_leave(&callback_guard);
    }
    (void)mtx_lock(&ctx->mutex);
    provider = ais_provider_find(ctx, provider_id);
    if (provider != NULL) {
        provider->lifecycle_in_progress = 0u;
        provider->last_error = status;
        provider->started = 0u;
        provider->availability = status == AIS_OK ?
            AIS_AVAIL_STOPPED : AIS_AVAIL_ERROR;
        for (size_t index = 0u; index < ctx->sensor_count; ++index) {
            if (ctx->sensors[index].provider_id == provider_id) {
                ctx->sensors[index].availability = provider->availability;
                ctx->sensors[index].last_error = status;
            }
        }
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_dispatch_event(
        ctx, AIS_EVENT_PROVIDER_AVAILABILITY, status, 0u, provider_id,
        0u, status == AIS_OK ? AIS_AVAIL_STOPPED : AIS_AVAIL_ERROR);
    ais_ctx_leave(ctx);
    if (status != AIS_OK) {
        return ais_error_set(
            error, status, ais_status_is_retryable(status),
            NULL, NULL, "provider_stop", ais_status_message(status));
    }
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_provider_get_status(
    ais_ctx_t *ctx, ais_provider_id_t provider_id,
    ais_provider_status_t *out_status, ais_error_info_t *error)
{
    ais_provider_entry_t *provider;
    if (out_status == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "provider_status", "missing output status");
    }
    if (!ais_header_valid(
            &out_status->header, sizeof(*out_status))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "provider_status", "invalid output header");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_status", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    provider = ais_provider_find(ctx, provider_id);
    if (provider == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, NULL,
            "provider_status", "provider is not registered");
    }
    out_status->availability = provider->availability;
    out_status->last_error = provider->last_error;
    out_status->observation_count = provider->observation_count;
    out_status->rejected_count = provider->rejected_count;
    out_status->started = provider->started;
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_provider_set_availability(
    ais_ctx_t *ctx, ais_provider_id_t provider_id, uint32_t availability,
    uint32_t error_code, ais_error_info_t *error)
{
    ais_provider_entry_t *provider;
    ais_provider_id_t event_provider_id;
    ais_status_t event_status;
    if (availability < AIS_AVAIL_DISCOVERED ||
        availability > AIS_AVAIL_STOPPED) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "provider_availability", "invalid availability");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_availability", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_availability", "context is closing");
    }
    provider = ais_provider_find(ctx, provider_id);
    if (provider == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, NULL,
            "provider_availability", "provider is not registered");
    }
    provider->availability = availability;
    provider->last_error = error_code;
    event_provider_id = provider->id;
    event_status = error_code == AIS_OK ? AIS_OK : error_code;
    if (availability == AIS_AVAIL_STOPPED) {
        provider->started = 0u;
    } else if (availability == AIS_AVAIL_ACTIVE) {
        provider->started = 1u;
    }
    for (size_t index = 0u; index < ctx->sensor_count; ++index) {
        if (ctx->sensors[index].provider_id == provider_id) {
            ctx->sensors[index].availability = availability;
            ctx->sensors[index].last_error = error_code;
        }
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_dispatch_event(
        ctx, AIS_EVENT_PROVIDER_AVAILABILITY, event_status, 0u,
        event_provider_id, 0u, availability);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_provider_unregister(
    ais_ctx_t *ctx, ais_provider_id_t provider_id, ais_error_info_t *error)
{
    ais_provider_entry_t removed;
    size_t position = SIZE_MAX;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_unregister", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_unregister", "context is closing");
    }
    for (size_t index = 0u; index < ctx->provider_count; ++index) {
        if (ctx->providers[index].id == provider_id) {
            position = index;
            break;
        }
    }
    if (position == SIZE_MAX) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, NULL,
            "provider_unregister", "provider is not registered");
    }
    if (ctx->providers[position].implicit) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_unregister", "implicit provider cannot be removed");
    }
    if (ctx->providers[position].poll_in_progress) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_BUSY, 1u, NULL, NULL,
            "provider_unregister", "provider poll is in progress");
    }
    if (ctx->providers[position].lifecycle_in_progress) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_BUSY, 1u, NULL, NULL,
            "provider_unregister", "provider lifecycle is in progress");
    }
    for (size_t index = 0u; index < ctx->sensor_count; ++index) {
        if (ctx->sensors[index].provider_id == provider_id) {
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            return ais_error_set(
                error, AIS_E_CONFLICT, 0u, NULL, NULL,
                "provider_unregister", "provider still owns sensors");
        }
    }
    removed = ctx->providers[position];
    memmove(
        &ctx->providers[position], &ctx->providers[position + 1u],
        (ctx->provider_count - position - 1u) * sizeof(*ctx->providers));
    ctx->provider_count -= 1u;
    (void)mtx_unlock(&ctx->mutex);
    if (removed.started && removed.vtable.stop != NULL) {
        ais_callback_guard_t callback_guard;
        ais_callback_enter(ctx, AIS_CALLBACK_PROVIDER, &callback_guard);
        (void)removed.vtable.stop(removed.user_data);
        ais_callback_leave(&callback_guard);
    }
    if (removed.vtable.destroy != NULL) {
        ais_callback_guard_t callback_guard;
        ais_callback_enter(ctx, AIS_CALLBACK_PROVIDER, &callback_guard);
        removed.vtable.destroy(removed.user_data);
        ais_callback_leave(&callback_guard);
    }
    ais_provider_entry_free(&removed);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_provider_poll(
    ais_ctx_t *ctx, ais_provider_id_t provider_id, ais_error_info_t *error)
{
    ais_provider_entry_t *provider;
    ais_provider_poll_fn poll;
    void *user_data;
    ais_status_t status = AIS_OK;
    uint32_t availability_event = 0u;
    ais_callback_guard_t callback_guard;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_poll", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_poll", "context is closing");
    }
    provider = ais_provider_find(ctx, provider_id);
    if (provider == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, NULL,
            "provider_poll", "provider is not registered");
    }
    if (!provider->started ||
        provider->availability != AIS_AVAIL_ACTIVE) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_PROVIDER_UNAVAILABLE, 1u, provider, NULL,
            "provider_poll", "provider is not active");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    if (provider->poll_in_progress) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_BUSY, 1u, provider, NULL,
            "provider_poll", "provider poll is already running");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    poll = provider->vtable.poll;
    user_data = provider->user_data;
    if (poll == NULL) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_UNSUPPORTED, 0u, provider, NULL,
            "provider_poll", "provider has no poll callback");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    provider->poll_in_progress = 1u;
    atomic_store_explicit(
        &provider->emit_context->active, true, memory_order_release);
    ais_provider_emit_context_t *emit_context = provider->emit_context;
    (void)mtx_unlock(&ctx->mutex);

    ais_callback_enter(ctx, AIS_CALLBACK_PROVIDER, &callback_guard);
    status = poll(user_data, ais_provider_emit_trampoline, emit_context);
    ais_callback_leave(&callback_guard);
    atomic_store_explicit(
        &emit_context->active, false, memory_order_release);

    (void)mtx_lock(&ctx->mutex);
    provider = ais_provider_find(ctx, provider_id);
    if (provider != NULL) {
        provider->poll_in_progress = 0u;
        if (status != AIS_OK) {
            provider->last_error = status;
            if (status == AIS_E_PROVIDER_UNAVAILABLE) {
                provider->availability = AIS_AVAIL_DEGRADED;
                availability_event = AIS_AVAIL_DEGRADED;
            }
        }
        (void)cnd_broadcast(&ctx->condition);
    }
    (void)mtx_unlock(&ctx->mutex);
    if (availability_event != 0u) {
        ais_dispatch_event(
            ctx, AIS_EVENT_PROVIDER_AVAILABILITY, status, 0u, provider_id,
            0u, availability_event);
    }
    ais_ctx_leave(ctx);
    if (status != AIS_OK) {
        return ais_error_set(
            error, status, ais_status_is_retryable(status),
            NULL, NULL, "provider_poll", ais_status_message(status));
    }
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_sensor_unregister(
    ais_ctx_t *ctx, ais_sensor_id_t sensor_id, ais_error_info_t *error)
{
    size_t position = SIZE_MAX;
    ais_sensor_entry_t removed;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "sensor_unregister", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "sensor_unregister", "context is closing");
    }
    for (size_t index = 0u; index < ctx->sensor_count; ++index) {
        if (ctx->sensors[index].id == sensor_id) {
            position = index;
            break;
        }
    }
    if (position == SIZE_MAX) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_SENSOR, 0u, NULL, NULL,
            "sensor_unregister", "sensor is not registered");
    }
    for (size_t index = 0u; index < ctx->pending_count; ++index) {
        if (ctx->pending[index].sensor_id == sensor_id) {
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            return ais_error_set(
                error, AIS_E_CONFLICT, 0u, NULL, NULL,
                "sensor_unregister", "sensor has queued observations");
        }
    }
    removed = ctx->sensors[position];
    memmove(
        &ctx->sensors[position], &ctx->sensors[position + 1u],
        (ctx->sensor_count - position - 1u) * sizeof(*ctx->sensors));
    ctx->sensor_count -= 1u;
    (void)mtx_unlock(&ctx->mutex);
    ais_sensor_entry_free(&removed);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_sensor_get_status(
    ais_ctx_t *ctx, ais_sensor_id_t sensor_id,
    ais_sensor_status_t *out_status, ais_error_info_t *error)
{
    ais_sensor_entry_t *sensor;
    ais_provider_entry_t *provider;
    if (out_status == NULL ||
        !ais_header_valid(
            out_status != NULL ? &out_status->header : NULL,
            sizeof(*out_status))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "sensor_status", "invalid output status");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "sensor_status", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    sensor = ais_sensor_find(ctx, sensor_id);
    if (sensor == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_SENSOR, 0u, NULL, NULL,
            "sensor_status", "sensor is not registered");
    }
    provider = ais_provider_find(ctx, sensor->provider_id);
    out_status->availability = sensor->availability;
    out_status->last_error = sensor->last_error;
    out_status->observation_count = sensor->observation_count;
    out_status->rejected_count = sensor->rejected_count;
    out_status->latest_sequence = sensor->has_latest ?
        sensor->latest.sequence : 0u;
    out_status->has_latest = sensor->has_latest;
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    if (provider == NULL) {
        return ais_error_set(
            error, AIS_E_INTERNAL, 0u, NULL, NULL,
            "sensor_status", "sensor provider disappeared");
    }
    ais_error_clear(error);
    return AIS_OK;
}

static void ais_timestamp_to_view(
    const ais_owned_timestamp_t *source, ais_timestamp_t *target)
{
    ais_timestamp_init(target);
    if (source == NULL || target == NULL) {
        return;
    }
    target->value_seconds = source->value_seconds;
    target->domain = source->domain;
    target->clock_id = ais_owned_string_view(&source->clock_id);
    target->present = source->present;
    target->reliable = source->reliable;
}

static void ais_quality_to_view(
    const ais_owned_quality_t *source, ais_quality_t *target)
{
    ais_quality_init(target);
    if (source != NULL && target != NULL) {
        target->known = source->known;
        target->score = source->score;
        target->age_seconds = source->age_seconds;
        target->dropped_samples = source->dropped_samples;
    }
}

static void ais_provenance_to_view(
    const ais_owned_provenance_t *source, ais_provenance_t *target)
{
    ais_provenance_init(target);
    if (source != NULL && target != NULL) {
        target->source_type =
            ais_owned_string_view(&source->source_type);
        target->provider_version =
            ais_owned_string_view(&source->provider_version);
        target->rule_or_model =
            ais_owned_string_view(&source->rule_or_model);
        target->source_observation_id =
            ais_owned_string_view(&source->source_observation_id);
    }
}

static void ais_payload_to_view(
    const ais_owned_payload_t *source, ais_payload_view_t *target)
{
    ais_payload_init(target);
    if (source == NULL || target == NULL) {
        return;
    }
    target->kind = source->kind;
    switch (source->kind) {
    case AIS_PAYLOAD_BOOL:
        target->as.bool_value = source->as.bool_value;
        break;
    case AIS_PAYLOAD_INT:
        target->as.int_value = source->as.int_value;
        break;
    case AIS_PAYLOAD_REAL:
        target->as.real_value = source->as.real_value;
        break;
    case AIS_PAYLOAD_VECTOR:
        target->as.vector = source->as.vector;
        break;
    case AIS_PAYLOAD_EVENT:
        target->as.event.name =
            ais_owned_string_view(&source->as.event.name);
        target->as.event.value =
            ais_owned_string_view(&source->as.event.value);
        break;
    case AIS_PAYLOAD_STRUCT:
        target->as.structure.label =
            ais_owned_string_view(&source->as.structure.label);
        target->as.structure.x = source->as.structure.x;
        target->as.structure.y = source->as.structure.y;
        target->as.structure.width = source->as.structure.width;
        target->as.structure.height = source->as.structure.height;
        target->as.structure.aux =
            ais_owned_string_view(&source->as.structure.aux);
        break;
    case AIS_PAYLOAD_BYTES:
        target->as.bytes.data = source->as.bytes.data;
        target->as.bytes.size = source->as.bytes.size;
        break;
    default:
        break;
    }
}

static void ais_observation_to_view(
    const ais_owned_observation_t *source, ais_observation_view_t *target)
{
    if (target == NULL) {
        return;
    }
    ais_observation_view_init(target);
    if (source == NULL) {
        return;
    }
    target->observation_id =
        ais_owned_string_view(&source->observation_id);
    target->sensor_id = source->sensor_id;
    target->provider_id = source->provider_id;
    target->session_id = ais_owned_string_view(&source->session_id);
    target->observation_type =
        ais_owned_string_view(&source->observation_type);
    target->acquisition = source->acquisition;
    ais_timestamp_to_view(&source->acquired_at, &target->acquired_at);
    ais_timestamp_to_view(&source->received_at, &target->received_at);
    target->sequence = source->sequence;
    ais_payload_to_view(&source->payload, &target->payload);
    target->unit_code = source->unit_code;
    target->unit_id = ais_owned_string_view(&source->unit_id);
    target->reference_frame_code = source->reference_frame_code;
    target->reference_frame_id =
        ais_owned_string_view(&source->reference_frame_id);
    target->validity = source->validity;
    ais_quality_to_view(&source->quality, &target->quality);
    ais_provenance_to_view(&source->provenance, &target->provenance);
}

static int ais_observation_view_base_valid(
    const ais_observation_view_t *view)
{
    return view != NULL &&
        ais_header_valid(&view->header, sizeof(*view)) &&
        view->sensor_id != 0u &&
        ais_header_valid(&view->acquired_at.header, sizeof(ais_timestamp_t)) &&
        ais_header_valid(&view->received_at.header, sizeof(ais_timestamp_t)) &&
        ais_header_valid(&view->payload.header, sizeof(ais_payload_view_t)) &&
        ais_header_valid(&view->quality.header, sizeof(ais_quality_t)) &&
        ais_header_valid(
            &view->provenance.header, sizeof(ais_provenance_t));
}

static int ais_owned_observation_from_view(
    ais_ctx_t *ctx, ais_sensor_entry_t *sensor,
    ais_provider_entry_t *provider, const ais_observation_view_t *view,
    ais_owned_observation_t *target, uint8_t reserve_sequence,
    ais_error_info_t *error)
{
    ais_timestamp_t receipt;
    ais_string_view_t observation_id;
    ais_string_view_t session;
    ais_string_view_t schema;
    ais_string_view_t source_type;
    ais_string_view_t provider_version;
    char generated_id[64];
    if (ctx == NULL || sensor == NULL || provider == NULL ||
        !ais_observation_view_base_valid(view) || target == NULL) {
        (void)ais_error_set_entry(
            error, AIS_E_INVALID_ARGUMENT, 0u, provider, sensor,
            "observation_build", "invalid observation view");
        return 0;
    }
    if (view->provider_id != 0u && view->provider_id != provider->id) {
        (void)ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "observation_build", "provider identity differs from sensor");
        return 0;
    }
    if (provider->id != sensor->provider_id) {
        (void)ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "observation_build", "provider does not own sensor");
        return 0;
    }
    session = view->session_id.size > 0u ?
        view->session_id : ais_owned_string_view(&sensor->session_id);
    schema = view->observation_type.size > 0u ?
        view->observation_type : ais_owned_string_view(&sensor->schema);
    if (!ais_text_valid(session, 1u) ||
        !ais_text_valid(schema, 1u) ||
        !ais_text_valid(view->observation_id, 0u)) {
        (void)ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "observation_build", "invalid observation identity");
        return 0;
    }
    if (view->observation_id.size >= AIS_MAX_TEXT_COPY) {
        (void)ais_error_set_entry(
            error, AIS_E_BUFFER_TOO_SMALL, 1u, provider, sensor,
            "observation_build", "observation identifier is too large");
        return 0;
    }
    if (!ais_string_equal(&sensor->session_id, session) ||
        !ais_string_equal(&sensor->schema, schema)) {
        (void)ais_error_set_entry(
            error, AIS_E_SCHEMA_MISMATCH, 0u, provider, sensor,
            "observation_build", "observation differs from sensor");
        return 0;
    }
    if (view->received_at.present &&
        (ctx->flags & AIS_CTX_ALLOW_EXTERNAL_RECEIPT) == 0u) {
        (void)ais_error_set_entry(
            error, AIS_E_INVALID_ARGUMENT, 0u, provider, sensor,
            "observation_build", "external receipt timestamp is disabled");
        return 0;
    }
    if (!ais_payload_view_well_formed(&view->payload, ctx->max_payload)) {
        (void)ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "observation_build", "invalid payload");
        return 0;
    }
    memset(target, 0, sizeof(*target));
    target->sensor_id = sensor->id;
    target->provider_id = provider->id;
    target->acquisition = view->acquisition == 0u ?
        sensor->acquisition_mode : view->acquisition;
    target->sequence = view->sequence == 0u ?
        sensor->next_sequence : view->sequence;
    if (reserve_sequence && view->sequence == 0u &&
        sensor->sequence_exhausted) {
        (void)ais_error_set_entry(
            error, AIS_E_BUFFER_TOO_SMALL, 1u, provider, sensor,
            "observation_build", "sensor sequence space exhausted");
        return 0;
    }
    if (reserve_sequence && view->sequence == 0u &&
        sensor->next_sequence != UINT64_MAX) {
        sensor->next_sequence += 1u;
    } else if (reserve_sequence && view->sequence == 0u) {
        sensor->sequence_exhausted = 1u;
    }
    target->unit_code = view->unit_code == 0u &&
        view->unit_id.size == 0u ? sensor->unit_code : view->unit_code;
    target->reference_frame_code = view->reference_frame_code == 0u &&
        view->reference_frame_id.size == 0u ?
        sensor->reference_frame_code : view->reference_frame_code;
    if (view->unit_id.size > 0u) {
        target->unit_code = ais_unit_code(view->unit_id);
    }
    if (view->reference_frame_id.size > 0u) {
        target->reference_frame_code =
            ais_reference_code(view->reference_frame_id);
    }
    target->validity = view->validity == 0u ?
        AIS_VALIDITY_VALID : view->validity;
    target->quality.known = view->quality.known;
    target->quality.score = view->quality.score;
    target->quality.age_seconds = view->quality.age_seconds;
    target->quality.dropped_samples = view->quality.dropped_samples;
    observation_id = view->observation_id;
    if (observation_id.size == 0u) {
        uint64_t id = ctx->next_observation_id;
        int written;
        if (reserve_sequence && id == UINT64_MAX) {
            (void)ais_error_set_entry(
                error, AIS_E_BUFFER_TOO_SMALL, 1u, provider, sensor,
                "observation_build", "observation identity space exhausted");
            return 0;
        }
        if (!reserve_sequence) {
            observation_id = AIS_SV("ais-validation-observation");
        } else {
            written = snprintf(
                generated_id, sizeof(generated_id),
                "ais-observation-%" PRIu64, id);
            ctx->next_observation_id += 1u;
            if (written <= 0 || (size_t)written >= sizeof(generated_id)) {
                return 0;
            }
            observation_id = (ais_string_view_t){
                generated_id, (size_t)written
            };
        }
    }
    source_type = view->provenance.source_type.size > 0u ?
        view->provenance.source_type : ais_owned_string_view(
            &provider->key);
    provider_version = view->provenance.provider_version.size > 0u ?
        view->provenance.provider_version : ais_owned_string_view(
            &provider->version);
    if (!ais_owned_string_set(
            &target->observation_id, observation_id) ||
        !ais_owned_string_set(&target->session_id, session) ||
        !ais_owned_string_set(&target->observation_type, schema) ||
        !ais_owned_string_set(&target->unit_id,
            view->unit_id.size > 0u ? view->unit_id :
                ais_owned_string_view(&sensor->unit_id)) ||
        !ais_owned_string_set(&target->reference_frame_id,
            view->reference_frame_id.size > 0u ? view->reference_frame_id :
                ais_owned_string_view(&sensor->reference_frame_id)) ||
        !ais_owned_payload_copy(
            &target->payload, &view->payload, ctx->max_payload) ||
        !ais_owned_provenance_copy(
            &target->provenance, &(ais_provenance_t){
                { sizeof(ais_provenance_t), AIS_STRUCT_VERSION_1 },
                source_type, provider_version,
                view->provenance.rule_or_model,
                view->provenance.source_observation_id })) {
        ais_owned_observation_free(target);
        (void)ais_error_set_entry(
            error, AIS_E_OUT_OF_MEMORY, 1u, provider, sensor,
            "observation_build", "observation copy failed");
        return 0;
    }
    if (!ais_owned_timestamp_copy(
            &target->acquired_at, &view->acquired_at)) {
        ais_owned_observation_free(target);
        (void)ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "observation_build", "invalid acquisition timestamp");
        return 0;
    }
    if (view->received_at.present) {
        if (!ais_owned_timestamp_copy(
                &target->received_at, &view->received_at)) {
            ais_owned_observation_free(target);
            (void)ais_error_set_entry(
                error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
                "observation_build", "invalid receipt timestamp");
            return 0;
        }
    } else {
        ais_timestamp_init(&receipt);
        if (!ais_now_timestamp(&target->received_at)) {
            ais_owned_observation_free(target);
            (void)ais_error_set_entry(
                error, AIS_E_INTERNAL, 0u, provider, sensor,
                "observation_build", "process clock unavailable");
            return 0;
        }
        (void)receipt;
    }
    target->flags = 0u;
    return 1;
}

static ais_status_t ais_owned_observation_validate(
    ais_ctx_t *ctx, ais_sensor_entry_t *sensor,
    ais_provider_entry_t *provider, const ais_owned_observation_t *observation,
    ais_error_info_t *error)
{
    const ais_builtin_schema_t *builtin = NULL;
    ais_schema_entry_t *custom = NULL;
    uint32_t allowed = 0u;
    uint32_t expected_modality = 0u;
    uint32_t expected_unit = 0u;
    uint32_t expected_reference = 0u;
    uint32_t expected_acquisition = 0u;
    uint32_t expected_capabilities = 0u;
    ais_string_view_t expected_unit_id = { NULL, 0u };
    ais_string_view_t expected_reference_id = { NULL, 0u };
    (void)expected_modality;
    (void)expected_capabilities;
    if (ctx == NULL || sensor == NULL || provider == NULL ||
        observation == NULL) {
        return ais_error_set_entry(
            error, AIS_E_INVALID_ARGUMENT, 0u, provider, sensor,
            "validate", "missing observation context");
    }
    if (!ais_text_valid(
            ais_owned_string_view(&observation->observation_id), 1u) ||
        observation->sensor_id != sensor->id ||
        observation->provider_id != provider->id ||
        !ais_text_valid(
            ais_owned_string_view(&observation->session_id), 1u) ||
        !ais_text_valid(
            ais_owned_string_view(&observation->observation_type), 1u) ||
        observation->acquisition < AIS_ACQUISITION_POINT ||
        observation->acquisition > AIS_ACQUISITION_STREAM ||
        observation->sequence == 0u ||
        observation->validity < AIS_VALIDITY_VALID ||
        observation->validity > AIS_VALIDITY_INCOMPLETE ||
        !ais_timestamp_valid(&observation->acquired_at) ||
        !ais_timestamp_valid(&observation->received_at) ||
        !ais_quality_valid(&observation->quality) ||
        !ais_text_valid(
            ais_owned_string_view(&observation->provenance.source_type), 1u)) {
        return ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "validate", "invalid observation envelope");
    }
    if (!ais_string_equal(
            &sensor->session_id,
            ais_owned_string_view(&observation->session_id)) ||
        !ais_string_equal(
            &sensor->schema,
            ais_owned_string_view(&observation->observation_type))) {
        return ais_error_set_entry(
            error, AIS_E_SCHEMA_MISMATCH, 0u, provider, sensor,
            "validate", "observation identity is not declared");
    }
    if (!ais_schema_resolve(
            ctx, ais_owned_string_view(&observation->observation_type),
            &builtin, &custom)) {
        return ais_error_set_entry(
            error, AIS_E_SCHEMA_MISMATCH, 0u, provider, sensor,
            "validate", "unknown observation schema");
    }
    if (builtin != NULL) {
        allowed = builtin->payload_mask;
        expected_unit = builtin->unit_code;
        expected_reference = builtin->reference_frame_code;
        expected_acquisition = builtin->acquisition;
        expected_unit_id = (ais_string_view_t){
            builtin->unit_id, strlen(builtin->unit_id)
        };
        expected_reference_id = (ais_string_view_t){
            builtin->reference_frame_id,
            strlen(builtin->reference_frame_id)
        };
    } else {
        allowed = custom->allowed_payload_kinds;
        expected_unit = sensor->unit_code;
        expected_reference = sensor->reference_frame_code;
        expected_acquisition = sensor->acquisition_mode;
        expected_unit_id = ais_owned_string_view(&sensor->unit_id);
        expected_reference_id =
            ais_owned_string_view(&sensor->reference_frame_id);
    }
    if (observation->payload.kind > 31u ||
        (allowed & AIS_PAYLOAD_MASK(observation->payload.kind)) == 0u ||
        observation->payload.kind == AIS_PAYLOAD_NONE) {
        return ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "validate", "payload kind is not allowed by schema");
    }
    if (observation->payload.kind == AIS_PAYLOAD_BYTES ||
        observation->payload.kind == AIS_PAYLOAD_IMAGE_FRAME ||
        observation->payload.kind == AIS_PAYLOAD_AUDIO_BLOCK ||
        observation->payload.kind == AIS_PAYLOAD_REFERENCE) {
        if (observation->payload.kind == AIS_PAYLOAD_BYTES &&
            ais_pointer_schema_event_type(
                ais_owned_string_view(&observation->observation_type)) != 0u) {
            ais_bytes_view_t pointer_bytes = {
                observation->payload.as.bytes.data,
                observation->payload.as.bytes.size
            };
            uint32_t expected_event_type = ais_pointer_schema_event_type(
                ais_owned_string_view(&observation->observation_type));
            if (!ais_pointer_event_bytes_valid(pointer_bytes, expected_event_type)) {
                return ais_error_set_entry(
                    error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
                    "validate", "invalid pointer event payload");
            }
        } else if (observation->payload.kind == AIS_PAYLOAD_BYTES &&
                   ais_desktop_schema_kind(
                       ais_owned_string_view(&observation->observation_type)) != 0u) {
            uint32_t expected_kind = ais_desktop_schema_kind(
                ais_owned_string_view(&observation->observation_type));
            ais_bytes_view_t desktop_bytes = {
                observation->payload.as.bytes.data,
                observation->payload.as.bytes.size
            };
            if (!ais_desktop_bytes_valid(
                    desktop_bytes, expected_kind)) {
                return ais_error_set_entry(
                    error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
                    "validate", "invalid desktop payload");
            }
        } else {
            return ais_error_set_entry(
                error, AIS_E_UNSUPPORTED, 0u, provider, sensor,
                "validate", "payload kind is reserved");
        }
    }
    if (observation->payload.kind == AIS_PAYLOAD_EVENT &&
        observation->payload.as.event.name.size == 0u) {
        return ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "validate", "event name is required");
    }
    if (observation->unit_code != sensor->unit_code ||
        !ais_owned_string_equal(
            &observation->unit_id, &sensor->unit_id) ||
        observation->reference_frame_code != sensor->reference_frame_code ||
        !ais_owned_string_equal(
            &observation->reference_frame_id, &sensor->reference_frame_id) ||
        observation->acquisition != sensor->acquisition_mode) {
        return ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "validate", "observation contract differs from sensor");
    }
    if (builtin != NULL &&
        (observation->unit_code != expected_unit ||
         !ais_owned_string_equal(
             &observation->unit_id, &(ais_owned_string_t){
                 (char *)expected_unit_id.data, expected_unit_id.size }) ||
         observation->reference_frame_code != expected_reference ||
         !ais_owned_string_equal(
             &observation->reference_frame_id, &(ais_owned_string_t){
                 (char *)expected_reference_id.data,
                 expected_reference_id.size }) ||
         observation->acquisition != expected_acquisition)) {
        if (strcmp(
                ais_builtin_find(
                    ais_owned_string_view(&observation->observation_type))->id,
                "desktop.output_geometry.v1") != 0) {
            return ais_error_set_entry(
                error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
                "validate", "built-in schema contract differs");
        }
    }
    if (builtin != NULL &&
        strcmp(
            builtin->id, "desktop.output_geometry.v1") == 0 &&
        (observation->unit_code != AIS_UNIT_PIXEL_LOGICAL ||
         observation->reference_frame_code != AIS_REF_SWAY_OUTPUT_LOGICAL)) {
        return ais_error_set_entry(
            error, AIS_E_INVALID_OBSERVATION, 0u, provider, sensor,
            "validate", "output geometry requires logical sway frame");
    }
    return AIS_OK;
}

static ais_status_t ais_run_schema_callback(
    ais_ctx_t *ctx, ais_schema_entry_t *schema,
    const ais_owned_observation_t *observation, ais_error_info_t *error)
{
    ais_observation_view_t view;
    ais_schema_validate_fn callback;
    void *user_data;
    ais_status_t status;
    ais_callback_guard_t guard;
    if (schema == NULL || schema->callbacks.validate == NULL) {
        return AIS_OK;
    }
    callback = schema->callbacks.validate;
    user_data = schema->user_data;
    ais_observation_to_view(observation, &view);
    ais_callback_enter(ctx, AIS_CALLBACK_SCHEMA, &guard);
    status = callback(user_data, &view, error);
    ais_callback_leave(&guard);
    ais_schema_callback_end(ctx, schema);
    return status;
}

static void ais_result_set_id(
    ais_ingest_result_t *result, ais_string_view_t observation_id)
{
    if (result == NULL) {
        return;
    }
    result->observation_id = observation_id;
}

static void ais_result_set_owned_id(
    ais_ctx_t *ctx, ais_ingest_result_t *result,
    ais_owned_string_t *observation_id)
{
    if (ctx == NULL || result == NULL || observation_id == NULL) {
        return;
    }
    ais_owned_string_free(&ctx->last_result_id);
    ctx->last_result_id = *observation_id;
    memset(observation_id, 0, sizeof(*observation_id));
    ais_result_set_id(
        result, ais_owned_string_view(&ctx->last_result_id));
}

static ais_status_t ais_result_prepare(
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    if (result == NULL) {
        return AIS_OK;
    }
    if (!ais_header_valid(
            &result->header, sizeof(*result))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "publish", "invalid ingest result header");
    }
    result->disposition = AIS_INGEST_REJECTED;
    result->flags = 0u;
    result->sequence = 0u;
    result->observation_id = (ais_string_view_t){ NULL, 0u };
    return AIS_OK;
}

static int ais_sensor_history_contains(
    const ais_sensor_entry_t *sensor, uint64_t sequence)
{
    if (sensor == NULL) {
        return 0;
    }
    for (size_t index = 0u; index < sensor->sequence_count; ++index) {
        if (sensor->sequence_history[index] == sequence) {
            return 1;
        }
    }
    return 0;
}

static int ais_sensor_history_add(
    ais_sensor_entry_t *sensor, uint64_t sequence, size_t limit)
{
    if (sensor == NULL || limit == 0u) {
        return 0;
    }
    if (!ais_array_grow(
            (void **)&sensor->sequence_history,
            &sensor->sequence_capacity, sensor->sequence_count + 1u,
            sizeof(*sensor->sequence_history))) {
        return 0;
    }
    if (sensor->sequence_count >= limit) {
        memmove(
            sensor->sequence_history, sensor->sequence_history + 1u,
            (sensor->sequence_count - 1u) *
                sizeof(*sensor->sequence_history));
        sensor->sequence_count -= 1u;
    }
    sensor->sequence_history[sensor->sequence_count++] = sequence;
    return 1;
}

static int ais_sensor_history_prepare(
    ais_sensor_entry_t *sensor, size_t limit)
{
    if (sensor == NULL || limit == 0u) {
        return 0;
    }
    return ais_array_grow(
        (void **)&sensor->sequence_history,
        &sensor->sequence_capacity, sensor->sequence_count + 1u,
        sizeof(*sensor->sequence_history));
}

static int ais_recent_reserve(ais_ctx_t *ctx)
{
    return ais_array_grow(
        (void **)&ctx->recent, &ctx->recent_capacity,
        ctx->recent_count + 1u, sizeof(*ctx->recent));
}

static int ais_percept_reserve(ais_ctx_t *ctx)
{
    return ais_array_grow(
        (void **)&ctx->percepts, &ctx->percept_capacity,
        ctx->percept_count + 1u, sizeof(*ctx->percepts));
}

static int ais_pending_reserve(ais_ctx_t *ctx)
{
    if (ctx->pending_count >= ctx->max_recent) {
        return 0;
    }
    return ais_array_grow(
        (void **)&ctx->pending, &ctx->pending_capacity,
        ctx->pending_count + 1u, sizeof(*ctx->pending));
}

static int ais_percept_from_observation(
    const ais_owned_observation_t *observation,
    ais_owned_percept_t *percept)
{
    const char *type = NULL;
    const char *rule = NULL;
    if (observation == NULL || percept == NULL) {
        return 0;
    }
    if (ais_string_equal(
            &observation->observation_type,
            AIS_SV("interaction.pointer_press.v1")) ||
        ais_string_equal(
            &observation->observation_type,
            AIS_SV("interaction.pointer_release.v1")) ||
        ais_string_equal(
            &observation->observation_type,
            AIS_SV("interaction.pointer_move_local.v1")) ||
        ais_string_equal(
            &observation->observation_type,
            AIS_SV("interaction.pointer_enter.v1")) ||
        ais_string_equal(
            &observation->observation_type,
            AIS_SV("interaction.pointer_leave.v1")) ||
        ais_string_equal(
            &observation->observation_type,
            AIS_SV("interaction.pointer_deactivate.v1")) ||
        ais_string_equal(
            &observation->observation_type,
            AIS_SV("interaction.button.v1"))) {
        type = "percept.local_interaction.v1";
        rule = "deterministic.interaction.v1";
    } else if (ais_string_equal(
                   &observation->observation_type,
                   AIS_SV("desktop.workspace_snapshot.v1"))) {
        type = "percept.desktop_workspace_observed.v1";
        rule = "deterministic.workspace_snapshot.v1";
    } else if (ais_string_equal(
                   &observation->observation_type,
                   AIS_SV("desktop.window_geometry.v1"))) {
        type = "percept.desktop_window_observed.v1";
        rule = "deterministic.window_snapshot.v1";
    } else if (ais_string_equal(
                   &observation->observation_type,
                   AIS_SV("desktop.output_geometry.v1"))) {
        type = "percept.desktop_output_observed.v1";
        rule = "deterministic.output_snapshot.v1";
    } else {
        return 0;
    }
    memset(percept, 0, sizeof(*percept));
    percept->validity = AIS_VALIDITY_INFERRED;
    percept->quality = observation->quality;
    percept->source_provider_id = observation->provider_id;
    percept->source_sensor_id = observation->sensor_id;
    if (!ais_owned_string_set_literal(&percept->type, type) ||
        !ais_owned_payload_clone(&percept->payload, &observation->payload) ||
        !ais_owned_string_set(
            &percept->source_observation_id,
            ais_owned_string_view(&observation->observation_id)) ||
        !ais_owned_string_set_literal(&percept->rule_or_model, rule) ||
        !ais_owned_timestamp_clone(
            &percept->generated_at, &observation->received_at) ||
        !ais_owned_string_set(
            &percept->source_type,
            ais_owned_string_view(&observation->provenance.source_type)) ||
        !ais_owned_string_set(
            &percept->source_session_id,
            ais_owned_string_view(&observation->session_id)) ||
        !ais_owned_string_set(
            &percept->source_schema,
            ais_owned_string_view(&observation->observation_type))) {
        ais_owned_percept_free(percept);
        return -1;
    }
    return 1;
}

static ais_status_t ais_commit_observation_locked(
    ais_ctx_t *ctx, ais_sensor_entry_t *sensor,
    ais_provider_entry_t *provider, ais_owned_observation_t *observation,
    uint8_t *percept_generated)
{
    ais_owned_observation_t recent_copy;
    ais_owned_observation_t latest_copy;
    ais_owned_percept_t percept;
    uint8_t has_percept;
    int percept_result;
    if (percept_generated != NULL) {
        *percept_generated = 0u;
    }
    memset(&recent_copy, 0, sizeof(recent_copy));
    memset(&latest_copy, 0, sizeof(latest_copy));
    memset(&percept, 0, sizeof(percept));
    percept_result = ais_percept_from_observation(observation, &percept);
    if (percept_result < 0) {
        ais_owned_percept_free(&percept);
        return AIS_E_OUT_OF_MEMORY;
    }
    has_percept = percept_result > 0 ? 1u : 0u;
    if (!ais_owned_observation_clone(&recent_copy, observation) ||
        !ais_owned_observation_clone(&latest_copy, observation) ||
        !ais_recent_reserve(ctx) ||
        (has_percept && !ais_percept_reserve(ctx))) {
        ais_owned_observation_free(&recent_copy);
        ais_owned_observation_free(&latest_copy);
        ais_owned_percept_free(&percept);
        return AIS_E_OUT_OF_MEMORY;
    }
    if (ctx->recent_count >= ctx->max_recent) {
        ais_owned_observation_free(&ctx->recent[0]);
        memmove(
            &ctx->recent[0], &ctx->recent[1],
            (ctx->recent_count - 1u) * sizeof(*ctx->recent));
        ctx->recent_count -= 1u;
    }
    ctx->recent[ctx->recent_count++] = recent_copy;
    memset(&recent_copy, 0, sizeof(recent_copy));
    ais_owned_observation_free(&sensor->latest);
    sensor->latest = latest_copy;
    memset(&latest_copy, 0, sizeof(latest_copy));
    sensor->has_latest = 1u;
    sensor->observation_count += 1u;
    provider->observation_count += 1u;
    if (has_percept) {
        ctx->percepts[ctx->percept_count++] = percept;
        memset(&percept, 0, sizeof(percept));
        if (ctx->percept_count > ctx->max_recent) {
            ais_owned_percept_free(&ctx->percepts[0]);
            memmove(
                &ctx->percepts[0], &ctx->percepts[1],
                (ctx->percept_count - 1u) * sizeof(*ctx->percepts));
            ctx->percept_count -= 1u;
        }
        if (percept_generated != NULL) {
            *percept_generated = 1u;
        }
    }
    ais_owned_percept_free(&percept);
    return AIS_OK;
}

typedef struct ais_event_call {
    ais_event_entry_t *entry;
} ais_event_call_t;

static void ais_dispatch_event(
    ais_ctx_t *ctx, uint32_t type, ais_status_t status,
    ais_sensor_id_t sensor_id, ais_provider_id_t provider_id,
    uint64_t sequence, uint32_t flags)
{
    ais_event_call_t *calls = NULL;
    size_t call_count = 0u;
    size_t call_capacity = 0u;
    ais_event_view_t view;
    memset(&view, 0, sizeof(view));
    ais_header_init(&view.header, sizeof(view));
    view.type = type;
    view.status = status;
    view.sensor_id = sensor_id;
    view.provider_id = provider_id;
    view.sequence = sequence;
    view.flags = flags;
    (void)mtx_lock(&ctx->mutex);
    for (size_t index = 0u; index < ctx->event_count; ++index) {
        ais_event_entry_t *entry = ctx->events[index];
        if (entry != NULL && entry->active && (entry->mask & type) != 0u) {
            if (ais_array_grow(
                    (void **)&calls, &call_capacity, call_count + 1u,
                    sizeof(*calls))) {
                entry->in_flight += 1u;
                calls[call_count++].entry = entry;
            }
        }
    }
    (void)mtx_unlock(&ctx->mutex);
    for (size_t index = 0u; index < call_count; ++index) {
        ais_event_entry_t *entry = calls[index].entry;
        if (entry->callback != NULL) {
            ais_callback_guard_t callback_guard;
            ais_callback_enter(ctx, AIS_CALLBACK_EVENT, &callback_guard);
            entry->callback(entry->user_data, &view);
            ais_callback_leave(&callback_guard);
        }
        (void)mtx_lock(&ctx->mutex);
        if (entry->in_flight > 0u) {
            entry->in_flight -= 1u;
        }
        (void)cnd_broadcast(&ctx->condition);
        (void)mtx_unlock(&ctx->mutex);
    }
    free(calls);
}

static void ais_mark_rejected_locked(
    ais_ctx_t *ctx, ais_sensor_entry_t *sensor,
    ais_provider_entry_t *provider)
{
    ctx->rejected_observation_count += 1u;
    if (sensor != NULL) {
        sensor->rejected_count += 1u;
    }
    if (provider != NULL) {
        provider->rejected_count += 1u;
    }
}

static ais_status_t ais_ingest_observation(
    ais_ctx_t *ctx, const ais_observation_view_t *view,
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    ais_sensor_entry_t *sensor;
    ais_provider_entry_t *provider;
    ais_schema_entry_t *custom_schema = NULL;
    ais_owned_observation_t observation;
    ais_status_t status;
    uint8_t percept_generated = 0u;
    ais_schema_validate_fn callback = NULL;
    void *callback_user_data = NULL;
    uint32_t callback_kind = AIS_CALLBACK_NONE;
    ais_callback_guard_t callback_guard;
    uint64_t sequence;
    uint8_t out_of_order = 0u;
    uint8_t observation_built;
    if (view == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "publish", "missing observation");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "publish", "context is unavailable");
    }
    status = ais_result_prepare(result, error);
    if (status != AIS_OK) {
        ais_ctx_leave(ctx);
        return status;
    }
    memset(&observation, 0, sizeof(observation));
    (void)mtx_lock(&ctx->mutex);
    sensor = ais_sensor_find(ctx, view->sensor_id);
    if (sensor == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_SENSOR, 0u, NULL, NULL,
            "publish", "sensor is not registered");
    }
    provider = ais_provider_find(
        ctx, view->provider_id == 0u ? sensor->provider_id : view->provider_id);
    if (provider == NULL) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, sensor,
            "publish", "provider is not registered");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    if (provider->availability != AIS_AVAIL_ACTIVE &&
        provider->availability != AIS_AVAIL_AVAILABLE) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_PROVIDER_UNAVAILABLE, 1u, provider, sensor,
            "publish", "provider is unavailable");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    observation_built = (uint8_t)ais_owned_observation_from_view(
        ctx, sensor, provider, view, &observation, 1u, error);
    if (!observation_built) {
        ais_sensor_id_t event_sensor_id = sensor->id;
        ais_provider_id_t event_provider_id = provider->id;
        ais_status_t rejected_status;
        rejected_status = error != NULL ? error->code :
            AIS_E_INVALID_OBSERVATION;
        ais_mark_rejected_locked(ctx, sensor, provider);
        (void)mtx_unlock(&ctx->mutex);
        if (result != NULL) {
            result->disposition = AIS_INGEST_REJECTED;
        }
        ais_dispatch_event(
            ctx, AIS_EVENT_REJECTED, rejected_status, event_sensor_id,
            event_provider_id, 0u, 0u);
        ais_owned_observation_free(&observation);
        ais_ctx_leave(ctx);
        return rejected_status;
    }
    status = ais_owned_observation_validate(
        ctx, sensor, provider, &observation, error);
    if (status == AIS_OK) {
        custom_schema = ais_custom_schema_find(
            ctx, ais_owned_string_view(&observation.observation_type));
        if (custom_schema != NULL && custom_schema->callbacks.validate != NULL) {
            callback = custom_schema->callbacks.validate;
            callback_user_data = custom_schema->user_data;
            callback_kind = AIS_CALLBACK_SCHEMA;
            (void)ais_schema_callback_begin_locked(custom_schema);
        }
    }
    if (status != AIS_OK) {
        ais_sensor_id_t event_sensor_id = sensor->id;
        ais_provider_id_t event_provider_id = provider->id;
        uint64_t event_sequence = observation.sequence;
        ais_mark_rejected_locked(ctx, sensor, provider);
        if (result != NULL) {
            result->disposition = AIS_INGEST_REJECTED;
        }
        (void)mtx_unlock(&ctx->mutex);
        ais_dispatch_event(
            ctx, AIS_EVENT_REJECTED, status, event_sensor_id,
            event_provider_id, event_sequence, 0u);
        ais_owned_observation_free(&observation);
        ais_ctx_leave(ctx);
        return status;
    }
    if (callback != NULL) {
        ais_observation_view_t callback_view;
        ais_observation_to_view(&observation, &callback_view);
        (void)mtx_unlock(&ctx->mutex);
        ais_callback_enter(ctx, callback_kind, &callback_guard);
        status = callback(callback_user_data, &callback_view, error);
        ais_callback_leave(&callback_guard);
        ais_schema_callback_end(ctx, custom_schema);
        (void)mtx_lock(&ctx->mutex);
        if (status != AIS_OK || ctx->closing) {
            ais_status_t callback_status = status;
            ais_sensor_id_t event_sensor_id = observation.sensor_id;
            ais_provider_id_t event_provider_id = observation.provider_id;
            sensor = ais_sensor_find(ctx, observation.sensor_id);
            provider = ais_provider_find(ctx, observation.provider_id);
            if (sensor != NULL && provider != NULL) {
                ais_mark_rejected_locked(ctx, sensor, provider);
            }
            if (callback_status == AIS_OK) {
                callback_status = AIS_E_BUSY;
            }
            (void)mtx_unlock(&ctx->mutex);
            ais_dispatch_event(
                ctx, AIS_EVENT_REJECTED, callback_status, event_sensor_id,
                event_provider_id, observation.sequence, 0u);
            ais_owned_observation_free(&observation);
            ais_ctx_leave(ctx);
            if (status == AIS_OK) {
                return ais_error_set(
                    error, callback_status, 1u, NULL, NULL,
                    "publish", "context began closing");
            }
            return status;
        }
    }
    sensor = ais_sensor_find(ctx, observation.sensor_id);
    provider = ais_provider_find(ctx, observation.provider_id);
    if (sensor == NULL || provider == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        ais_owned_observation_free(&observation);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "publish", "identity disappeared during validation");
    }
    sequence = observation.sequence;
    if (ais_sensor_history_contains(sensor, sequence)) {
        ais_sensor_id_t event_sensor_id = sensor->id;
        ais_provider_id_t event_provider_id = provider->id;
        ais_status_t duplicate_status;
        uint8_t reject_duplicate = ctx->reject_duplicates;
        observation.flags |= AIS_FLAG_DUPLICATE;
        if (reject_duplicate) {
            ais_mark_rejected_locked(ctx, sensor, provider);
        }
        if (result != NULL) {
            result->disposition = AIS_INGEST_DUPLICATE;
            result->flags = observation.flags;
            result->sequence = sequence;
            ais_result_set_owned_id(
                ctx, result, &observation.observation_id);
        }
        duplicate_status = ais_error_set_entry(
            error, AIS_E_DUPLICATE, 0u, provider, sensor,
            "publish", "duplicate sequence");
        (void)mtx_unlock(&ctx->mutex);
        ais_dispatch_event(
            ctx, AIS_EVENT_DUPLICATE, AIS_E_DUPLICATE,
            event_sensor_id, event_provider_id, sequence, observation.flags);
        ais_owned_observation_free(&observation);
        ais_ctx_leave(ctx);
        if (reject_duplicate) {
            return duplicate_status;
        }
        ais_error_clear(error);
        return AIS_OK;
    }
    if (sensor->has_highest_sequence &&
        sequence < sensor->highest_sequence) {
        out_of_order = 1u;
        if (ctx->flag_out_of_order) {
            observation.flags |= AIS_FLAG_OUT_OF_ORDER;
        }
    } else if (sensor->has_highest_sequence &&
               ctx->retain_dropped_predecessor_flag &&
               sequence > sensor->highest_sequence &&
               sequence - sensor->highest_sequence > 1u) {
        observation.flags |= AIS_FLAG_DROPPED_PREDECESSOR;
    }
    if (!ais_sensor_history_prepare(
            sensor, ctx->dedup_history_limit)) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_OUT_OF_MEMORY, 1u, provider, sensor,
            "publish", "sequence history allocation failed");
        ais_mark_rejected_locked(ctx, sensor, provider);
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        ais_owned_observation_free(&observation);
        return failure;
    }
    if (ctx->processing_mode == AIS_PROCESS_MANUAL) {
        if (!ais_pending_reserve(ctx)) {
            ais_status_t failure = ais_error_set_entry(
                error, AIS_E_BUFFER_TOO_SMALL, 1u, provider, sensor,
                "publish", "manual processing queue is full");
            ais_mark_rejected_locked(ctx, sensor, provider);
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            ais_owned_observation_free(&observation);
            return failure;
        }
        memset(
            &ctx->pending[ctx->pending_count], 0,
            sizeof(ctx->pending[ctx->pending_count]));
        if (!ais_owned_observation_clone(
                &ctx->pending[ctx->pending_count], &observation)) {
            ais_status_t failure = ais_error_set_entry(
                error, AIS_E_OUT_OF_MEMORY, 1u, provider, sensor,
                "publish", "manual observation copy failed");
            ais_mark_rejected_locked(ctx, sensor, provider);
            (void)mtx_unlock(&ctx->mutex);
            ais_ctx_leave(ctx);
            ais_owned_observation_free(&observation);
            return failure;
        }
        ctx->pending_count += 1u;
        (void)ais_sensor_history_add(
            sensor, sequence, ctx->dedup_history_limit);
        if (!sensor->has_highest_sequence || sequence > sensor->highest_sequence) {
            sensor->highest_sequence = sequence;
            sensor->has_highest_sequence = 1u;
        }
        ais_sensor_advance_sequence_locked(sensor, sequence);
        if (result != NULL) {
            result->disposition = AIS_INGEST_QUEUED;
            result->flags = observation.flags;
            result->sequence = sequence;
            ais_result_set_id(
                result, ais_owned_string_view(
                    &ctx->pending[ctx->pending_count - 1u].observation_id));
        }
        (void)mtx_unlock(&ctx->mutex);
        ais_owned_observation_free(&observation);
        ais_ctx_leave(ctx);
        ais_error_clear(error);
        (void)out_of_order;
        return AIS_OK;
    }
    status = ais_commit_observation_locked(
        ctx, sensor, provider, &observation, &percept_generated);
    if (status == AIS_OK) {
        (void)ais_sensor_history_add(
            sensor, sequence, ctx->dedup_history_limit);
        if (!sensor->has_highest_sequence || sequence > sensor->highest_sequence) {
            sensor->highest_sequence = sequence;
            sensor->has_highest_sequence = 1u;
        }
        ais_sensor_advance_sequence_locked(sensor, sequence);
        if (result != NULL) {
            result->disposition = AIS_INGEST_ACCEPTED;
            result->flags = observation.flags;
            result->sequence = sequence;
            ais_result_set_id(
                result, ais_owned_string_view(
                    &ctx->recent[ctx->recent_count - 1u].observation_id));
        }
    } else {
        ais_mark_rejected_locked(ctx, sensor, provider);
    }
    ais_sensor_id_t event_sensor_id = sensor->id;
    ais_provider_id_t event_provider_id = provider->id;
    uint32_t event_flags = observation.flags;
    ais_status_t commit_status = status;
    if (status != AIS_OK) {
        commit_status = ais_error_set_entry(
            error, status, ais_status_is_retryable(status), provider, sensor,
            "publish", "observation commit failed");
    }
    (void)mtx_unlock(&ctx->mutex);
    if (status != AIS_OK) {
        ais_owned_observation_free(&observation);
        ais_dispatch_event(
            ctx, AIS_EVENT_REJECTED, commit_status, event_sensor_id,
            event_provider_id, sequence, event_flags);
        ais_ctx_leave(ctx);
        return commit_status;
    }
    ais_dispatch_event(
        ctx, AIS_EVENT_ACCEPTED, AIS_OK, event_sensor_id, event_provider_id,
        sequence, event_flags);
    if (percept_generated) {
        ais_dispatch_event(
            ctx, AIS_EVENT_PERCEPT_GENERATED, AIS_OK, event_sensor_id,
            event_provider_id, sequence, event_flags);
    }
    ais_owned_observation_free(&observation);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_publish_observation(
    ais_ctx_t *ctx, const ais_observation_view_t *observation,
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    return ais_ingest_observation(ctx, observation, result, error);
}

static ais_status_t ais_publish_scalar(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, ais_payload_view_t *payload,
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    ais_observation_view_t observation;
    ais_status_t status;
    if (payload == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "publish", "missing scalar payload");
    }
    ais_observation_view_init(&observation);
    observation.sensor_id = sensor;
    observation.payload = *payload;
    status = ais_publish_observation(ctx, &observation, result, error);
    return status;
}

AIS_API ais_status_t ais_publish_f64(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, double value,
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    ais_payload_view_t payload;
    ais_payload_init(&payload);
    if (ais_payload_set_real(&payload, value) != AIS_OK) {
        return ais_error_set(
            error, AIS_E_INVALID_OBSERVATION, 0u, NULL, NULL,
            "publish_f64", "value is not finite");
    }
    return ais_publish_scalar(ctx, sensor, &payload, result, error);
}

AIS_API ais_status_t ais_publish_i64(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, int64_t value,
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    ais_payload_view_t payload;
    ais_payload_init(&payload);
    (void)ais_payload_set_int(&payload, value);
    return ais_publish_scalar(ctx, sensor, &payload, result, error);
}

AIS_API ais_status_t ais_publish_bool(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, uint8_t value,
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    ais_payload_view_t payload;
    ais_payload_init(&payload);
    if (ais_payload_set_bool(&payload, value) != AIS_OK) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "publish_bool", "boolean must be zero or one");
    }
    return ais_publish_scalar(ctx, sensor, &payload, result, error);
}

AIS_API ais_status_t ais_publish_bytes(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, ais_bytes_view_t value,
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    ais_status_t result_status;
    (void)ctx;
    (void)sensor;
    (void)value;
    result_status = ais_result_prepare(result, error);
    if (result_status != AIS_OK) {
        return result_status;
    }
    if (result != NULL) {
        result->disposition = AIS_INGEST_REJECTED;
    }
    return ais_error_set(
        error, AIS_E_UNSUPPORTED, 0u, NULL, NULL,
        "publish_bytes", "bytes payload contract is reserved");
}

static ais_status_t ais_prepare_for_validation(
    ais_ctx_t *ctx, const ais_observation_view_t *view,
    ais_owned_observation_t *observation, ais_sensor_entry_t **out_sensor,
    ais_provider_entry_t **out_provider, ais_schema_entry_t **out_schema,
    ais_error_info_t *error)
{
    ais_sensor_entry_t *sensor;
    ais_provider_entry_t *provider;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "validate", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    sensor = ais_sensor_find(ctx, view != NULL ? view->sensor_id : 0u);
    if (sensor == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_SENSOR, 0u, NULL, NULL,
            "validate", "sensor is not registered");
    }
    provider = ais_provider_find(
        ctx, view->provider_id == 0u ? sensor->provider_id :
            view->provider_id);
    if (provider == NULL) {
        ais_status_t failure = ais_error_set_entry(
            error, AIS_E_UNKNOWN_PROVIDER, 0u, NULL, sensor,
            "validate", "provider is not registered");
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    if (!ais_owned_observation_from_view(
            ctx, sensor, provider, view, observation, 0u, error)) {
        ais_status_t failure = error != NULL ? error->code :
            AIS_E_INVALID_OBSERVATION;
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return failure;
    }
    ais_status_t status = ais_owned_observation_validate(
        ctx, sensor, provider, observation, error);
    if (status == AIS_OK && out_schema != NULL) {
        *out_schema = ais_custom_schema_find(
            ctx, ais_owned_string_view(&observation->observation_type));
        (void)ais_schema_callback_begin_locked(*out_schema);
    }
    if (out_sensor != NULL) {
        *out_sensor = sensor;
    }
    if (out_provider != NULL) {
        *out_provider = provider;
    }
    (void)mtx_unlock(&ctx->mutex);
    if (status != AIS_OK) {
        ais_ctx_leave(ctx);
        ais_owned_observation_free(observation);
        return status;
    }
    return AIS_OK;
}

AIS_API ais_status_t ais_observation_validate(
    ais_ctx_t *ctx, const ais_observation_view_t *observation,
    ais_error_info_t *error)
{
    ais_owned_observation_t owned;
    ais_sensor_entry_t *sensor = NULL;
    ais_provider_entry_t *provider = NULL;
    ais_schema_entry_t *schema = NULL;
    ais_status_t status;
    uint8_t context_held;
    memset(&owned, 0, sizeof(owned));
    if (observation == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "validate", "missing observation");
    }
    status = ais_prepare_for_validation(
        ctx, observation, &owned, &sensor, &provider, &schema, error);
    context_held = status == AIS_OK ? 1u : 0u;
    if (status == AIS_OK && schema != NULL) {
        status = ais_run_schema_callback(ctx, schema, &owned, error);
    }
    ais_owned_observation_free(&owned);
    if (context_held) {
        ais_ctx_leave(ctx);
    }
    if (status == AIS_OK) {
        ais_error_clear(error);
    }
    return status;
}

static ais_status_t ais_provider_emit_trampoline(
    void *emit_context, const ais_observation_view_t *observation,
    ais_ingest_result_t *result, ais_error_info_t *error)
{
    ais_provider_emit_context_t *emission =
        (ais_provider_emit_context_t *)emit_context;
    ais_observation_view_t authorized;
    if (emission == NULL ||
        !atomic_load_explicit(&emission->active, memory_order_acquire)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "provider_emit", "provider emission is no longer active");
    }
    if (observation == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "provider_emit", "missing observation");
    }
    authorized = *observation;
    if (authorized.provider_id == 0u) {
        authorized.provider_id = emission->provider_id;
    } else if (authorized.provider_id != emission->provider_id) {
        return ais_error_set(
            error, AIS_E_INVALID_OBSERVATION, 0u, NULL, NULL,
            "provider_emit", "provider identity differs from poll owner");
    }
    return ais_publish_observation(
        emission->ctx, &authorized, result, error);
}

static ais_status_t ais_latest_check_locked(
    ais_ctx_t *ctx, ais_sensor_entry_t *sensor,
    ais_provider_entry_t **out_provider, ais_error_info_t *error)
{
    ais_provider_entry_t *provider;
    ais_owned_timestamp_t now;
    uint64_t age;
    if (ctx == NULL || sensor == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    provider = ais_provider_find(ctx, sensor->provider_id);
    if (out_provider != NULL) {
        *out_provider = provider;
    }
    if (provider == NULL) {
        return ais_error_set_entry(
            error, AIS_E_INTERNAL, 0u, NULL, sensor,
            "latest", "sensor provider disappeared");
    }
    if (provider->availability != AIS_AVAIL_ACTIVE &&
        provider->availability != AIS_AVAIL_AVAILABLE) {
        return ais_error_set_entry(
            error, AIS_E_PROVIDER_UNAVAILABLE, 1u, provider, sensor,
            "latest", "provider is unavailable");
    }
    if (!sensor->has_latest) {
        return ais_error_set_entry(
            error, AIS_E_NO_DATA, 0u, provider, sensor,
            "latest", "sensor has no accepted observation");
    }
    memset(&now, 0, sizeof(now));
    if (!ais_now_timestamp(&now)) {
        return ais_error_set_entry(
            error, AIS_E_INTERNAL, 0u, provider, sensor,
            "latest", "process clock unavailable");
    }
    if (!ais_timestamps_comparable(&now, &sensor->latest.received_at)) {
        ais_owned_timestamp_free(&now);
        return ais_error_set_entry(
            error, AIS_E_TIMESTAMP_UNCOMPARABLE, 0u, provider, sensor,
            "latest", "receipt clock cannot be compared");
    }
    age = ais_age_nanoseconds(&now, &sensor->latest.received_at);
    ais_owned_timestamp_free(&now);
    if (age > ctx->stale_after_ns) {
        return ais_error_set_entry(
            error, AIS_E_STALE, 0u, provider, sensor,
            "latest", "latest observation is stale");
    }
    return AIS_OK;
}

AIS_API ais_status_t ais_latest_f64(
    ais_ctx_t *ctx, ais_sensor_id_t sensor_id, double *out_value,
    ais_error_info_t *error)
{
    ais_sensor_entry_t *sensor;
    ais_provider_entry_t *provider = NULL;
    ais_status_t status;
    if (out_value == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "latest_f64", "missing output value");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "latest_f64", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    sensor = ais_sensor_find(ctx, sensor_id);
    if (sensor == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_SENSOR, 0u, NULL, NULL,
            "latest_f64", "sensor is not registered");
    }
    status = ais_latest_check_locked(ctx, sensor, &provider, error);
    if (status == AIS_OK &&
        sensor->latest.payload.kind != AIS_PAYLOAD_REAL) {
        status = ais_error_set_entry(
            error, AIS_E_TYPE_MISMATCH, 0u, provider, sensor,
            "latest_f64", "latest payload is not real");
    }
    if (status == AIS_OK) {
        *out_value = sensor->latest.payload.as.real_value;
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    if (status == AIS_OK) {
        ais_error_clear(error);
    }
    return status;
}

AIS_API ais_status_t ais_latest_i64(
    ais_ctx_t *ctx, ais_sensor_id_t sensor_id, int64_t *out_value,
    ais_error_info_t *error)
{
    ais_sensor_entry_t *sensor;
    ais_provider_entry_t *provider = NULL;
    ais_status_t status;
    if (out_value == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "latest_i64", "missing output value");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "latest_i64", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    sensor = ais_sensor_find(ctx, sensor_id);
    if (sensor == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_SENSOR, 0u, NULL, NULL,
            "latest_i64", "sensor is not registered");
    }
    status = ais_latest_check_locked(ctx, sensor, &provider, error);
    if (status == AIS_OK &&
        sensor->latest.payload.kind != AIS_PAYLOAD_INT) {
        status = ais_error_set_entry(
            error, AIS_E_TYPE_MISMATCH, 0u, provider, sensor,
            "latest_i64", "latest payload is not integer");
    }
    if (status == AIS_OK) {
        *out_value = sensor->latest.payload.as.int_value;
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    if (status == AIS_OK) {
        ais_error_clear(error);
    }
    return status;
}

AIS_API ais_status_t ais_latest_bool(
    ais_ctx_t *ctx, ais_sensor_id_t sensor_id, uint8_t *out_value,
    ais_error_info_t *error)
{
    ais_sensor_entry_t *sensor;
    ais_provider_entry_t *provider = NULL;
    ais_status_t status;
    if (out_value == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "latest_bool", "missing output value");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "latest_bool", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    sensor = ais_sensor_find(ctx, sensor_id);
    if (sensor == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_SENSOR, 0u, NULL, NULL,
            "latest_bool", "sensor is not registered");
    }
    status = ais_latest_check_locked(ctx, sensor, &provider, error);
    if (status == AIS_OK &&
        sensor->latest.payload.kind != AIS_PAYLOAD_BOOL) {
        status = ais_error_set_entry(
            error, AIS_E_TYPE_MISMATCH, 0u, provider, sensor,
            "latest_bool", "latest payload is not boolean");
    }
    if (status == AIS_OK) {
        *out_value = sensor->latest.payload.as.bool_value;
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    if (status == AIS_OK) {
        ais_error_clear(error);
    }
    return status;
}

AIS_API ais_status_t ais_latest_bytes(
    ais_ctx_t *ctx, ais_sensor_id_t sensor_id, void *buffer,
    size_t capacity, size_t *out_size, ais_error_info_t *error)
{
    (void)ctx;
    (void)sensor_id;
    (void)buffer;
    (void)capacity;
    if (out_size == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "latest_bytes", "missing required size output");
    }
    return ais_error_set(
        error, AIS_E_UNSUPPORTED, 0u, NULL, NULL,
        "latest_bytes", "bytes payload contract is reserved");
}

static uint64_t ais_ctx_unavailable_count_locked(const ais_ctx_t *ctx)
{
    uint64_t count = 0u;
    for (size_t index = 0u; index < ctx->provider_count; ++index) {
        uint32_t availability = ctx->providers[index].availability;
        if (availability != AIS_AVAIL_ACTIVE &&
            availability != AIS_AVAIL_AVAILABLE) {
            count += 1u;
        }
    }
    return count;
}

AIS_API ais_status_t ais_ctx_get_state_summary(
    ais_ctx_t *ctx, ais_state_summary_t *out_state,
    ais_error_info_t *error)
{
    if (out_state == NULL ||
        !ais_header_valid(
            out_state != NULL ? &out_state->header : NULL,
            sizeof(*out_state))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_summary", "invalid output state");
    }
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "state_summary", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    out_state->recent_observation_count = ctx->recent_count;
    out_state->active_percept_count = ctx->percept_count;
    out_state->unavailable_provider_count =
        ais_ctx_unavailable_count_locked(ctx);
    out_state->stale_observation_count = ctx->stale_observation_count;
    out_state->rejected_observation_count =
        ctx->rejected_observation_count;
    out_state->processing_pending = ctx->pending_count > 0u ? 1u : 0u;
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_ctx_refresh(
    ais_ctx_t *ctx, ais_error_info_t *error)
{
    ais_owned_timestamp_t now;
    ais_event_view_t *stale_events = NULL;
    size_t stale_count = 0u;
    size_t stale_capacity = 0u;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "refresh", "context is unavailable");
    }
    memset(&now, 0, sizeof(now));
    if (!ais_now_timestamp(&now)) {
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INTERNAL, 0u, NULL, NULL,
            "refresh", "process clock unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    if (ctx->closing) {
        (void)mtx_unlock(&ctx->mutex);
        ais_owned_timestamp_free(&now);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "refresh", "context is closing");
    }
    ctx->stale_observation_count = 0u;
    for (size_t index = 0u; index < ctx->recent_count; ++index) {
        ais_owned_observation_t *observation = &ctx->recent[index];
        if (ais_timestamps_comparable(&now, &observation->received_at) &&
            ais_age_nanoseconds(&now, &observation->received_at) >
                ctx->stale_after_ns) {
            uint8_t newly_stale =
                observation->validity != AIS_VALIDITY_STALE;
            observation->validity = AIS_VALIDITY_STALE;
            if (newly_stale &&
                ais_array_grow(
                    (void **)&stale_events, &stale_capacity,
                    stale_count + 1u, sizeof(*stale_events))) {
                ais_header_init(
                    &stale_events[stale_count].header,
                    sizeof(stale_events[stale_count]));
                stale_events[stale_count].type = AIS_EVENT_STALE_REFRESH;
                stale_events[stale_count].status = AIS_E_STALE;
                stale_events[stale_count].sensor_id = observation->sensor_id;
                stale_events[stale_count].provider_id =
                    observation->provider_id;
                stale_events[stale_count].sequence = observation->sequence;
                stale_events[stale_count].flags = observation->flags;
                stale_count += 1u;
            }
        }
        if (observation->validity == AIS_VALIDITY_STALE) {
            ctx->stale_observation_count += 1u;
        }
    }
    for (size_t index = 0u; index < ctx->sensor_count; ++index) {
        ais_sensor_entry_t *sensor = &ctx->sensors[index];
        if (sensor->has_latest &&
            ais_timestamps_comparable(&now, &sensor->latest.received_at) &&
            ais_age_nanoseconds(&now, &sensor->latest.received_at) >
                ctx->stale_after_ns) {
            sensor->latest.validity = AIS_VALIDITY_STALE;
        }
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_owned_timestamp_free(&now);
    for (size_t index = 0u; index < stale_count; ++index) {
        ais_dispatch_event(
            ctx, stale_events[index].type, stale_events[index].status,
            stale_events[index].sensor_id,
            stale_events[index].provider_id,
            stale_events[index].sequence, stale_events[index].flags);
    }
    free(stale_events);
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

static ais_status_t ais_observation_snapshot_create_locked(
    ais_ctx_t *ctx, const ais_owned_observation_t *source,
    ais_observation_snapshot_t **out_snapshot, ais_error_info_t *error)
{
    ais_observation_snapshot_t *snapshot;
    if (out_snapshot == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "observation_snapshot", "missing output snapshot");
    }
    *out_snapshot = NULL;
    snapshot = (ais_observation_snapshot_t *)calloc(1u, sizeof(*snapshot));
    if (snapshot == NULL ||
        !ais_owned_observation_clone(&snapshot->observation, source)) {
        if (snapshot != NULL) {
            free(snapshot);
        }
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "observation_snapshot", "snapshot allocation failed");
    }
    snapshot->lifetime = ctx->lifetime;
    ais_lifetime_retain(snapshot->lifetime);
    *out_snapshot = snapshot;
    return AIS_OK;
}

AIS_API ais_status_t ais_sensor_latest_observation(
    ais_ctx_t *ctx, ais_sensor_id_t sensor_id,
    ais_observation_snapshot_t **out_snapshot,
    ais_error_info_t *error)
{
    ais_sensor_entry_t *sensor;
    ais_status_t status;
    if (out_snapshot == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "latest_observation", "missing output snapshot");
    }
    *out_snapshot = NULL;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "latest_observation", "context is unavailable");
    }
    (void)mtx_lock(&ctx->mutex);
    sensor = ais_sensor_find(ctx, sensor_id);
    if (sensor == NULL) {
        (void)mtx_unlock(&ctx->mutex);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_UNKNOWN_SENSOR, 0u, NULL, NULL,
            "latest_observation", "sensor is not registered");
    }
    status = ais_latest_check_locked(ctx, sensor, NULL, error);
    if (status == AIS_OK) {
        status = ais_observation_snapshot_create_locked(
            ctx, &sensor->latest, out_snapshot, error);
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
    if (status == AIS_OK) {
        ais_error_clear(error);
    }
    return status;
}

AIS_API ais_status_t ais_observation_snapshot_get_view(
    const ais_observation_snapshot_t *snapshot,
    ais_observation_view_t *out_view, ais_error_info_t *error)
{
    if (snapshot == NULL || out_view == NULL ||
        !ais_header_valid(
            out_view != NULL ? &out_view->header : NULL,
            sizeof(*out_view))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "observation_snapshot_view", "invalid snapshot or output");
    }
    if (ais_lifetime_closed(snapshot->lifetime)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "observation_snapshot_view", "context is closed");
    }
    ais_observation_to_view(&snapshot->observation, out_view);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_observation_snapshot_release(
    ais_observation_snapshot_t **inout_snapshot,
    ais_error_info_t *error)
{
    ais_observation_snapshot_t *snapshot;
    if (inout_snapshot == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "observation_snapshot_release", "missing snapshot address");
    }
    if (*inout_snapshot == NULL) {
        ais_error_clear(error);
        return AIS_OK;
    }
    snapshot = *inout_snapshot;
    ais_owned_observation_free(&snapshot->observation);
    ais_lifetime_release(snapshot->lifetime);
    free(snapshot);
    *inout_snapshot = NULL;
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_ctx_state_snapshot(
    ais_ctx_t *ctx, ais_state_snapshot_t **out_snapshot,
    ais_error_info_t *error)
{
    ais_state_snapshot_t *snapshot;
    if (out_snapshot == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_snapshot", "missing output snapshot");
    }
    *out_snapshot = NULL;
    if (!ais_ctx_enter(ctx)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "state_snapshot", "context is unavailable");
    }
    snapshot = (ais_state_snapshot_t *)calloc(1u, sizeof(*snapshot));
    if (snapshot == NULL) {
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "state_snapshot", "snapshot allocation failed");
    }
    (void)mtx_lock(&ctx->mutex);
    snapshot->lifetime = ctx->lifetime;
    ais_lifetime_retain(snapshot->lifetime);
    if (ctx->recent_count > 0u) {
        snapshot->observations = (ais_owned_observation_t *)calloc(
            ctx->recent_count, sizeof(*snapshot->observations));
    }
    if (ctx->percept_count > 0u) {
        snapshot->percepts = (ais_owned_percept_t *)calloc(
            ctx->percept_count, sizeof(*snapshot->percepts));
    }
    if ((ctx->recent_count > 0u && snapshot->observations == NULL) ||
        (ctx->percept_count > 0u && snapshot->percepts == NULL)) {
        (void)mtx_unlock(&ctx->mutex);
        ais_lifetime_release(snapshot->lifetime);
        free(snapshot->observations);
        free(snapshot->percepts);
        free(snapshot);
        ais_ctx_leave(ctx);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "state_snapshot", "state copy allocation failed");
    }
    for (size_t index = 0u; index < ctx->recent_count; ++index) {
        if (!ais_owned_observation_clone(
                &snapshot->observations[index], &ctx->recent[index])) {
            (void)mtx_unlock(&ctx->mutex);
            for (size_t item = 0u; item < index; ++item) {
                ais_owned_observation_free(&snapshot->observations[item]);
            }
            free(snapshot->observations);
            free(snapshot->percepts);
            ais_lifetime_release(snapshot->lifetime);
            free(snapshot);
            ais_ctx_leave(ctx);
            return ais_error_set(
                error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
                "state_snapshot", "observation copy failed");
        }
    }
    snapshot->observation_count = ctx->recent_count;
    for (size_t index = 0u; index < ctx->percept_count; ++index) {
        if (!ais_owned_percept_clone(
                &snapshot->percepts[index], &ctx->percepts[index])) {
            (void)mtx_unlock(&ctx->mutex);
            for (size_t item = 0u; item < snapshot->observation_count; ++item) {
                ais_owned_observation_free(&snapshot->observations[item]);
            }
            for (size_t item = 0u; item < index; ++item) {
                ais_owned_percept_free(&snapshot->percepts[item]);
            }
            free(snapshot->observations);
            free(snapshot->percepts);
            ais_lifetime_release(snapshot->lifetime);
            free(snapshot);
            ais_ctx_leave(ctx);
            return ais_error_set(
                error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
                "state_snapshot", "percept copy failed");
        }
    }
    snapshot->percept_count = ctx->percept_count;
    (void)mtx_unlock(&ctx->mutex);
    *out_snapshot = snapshot;
    ais_ctx_leave(ctx);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_state_snapshot_release(
    ais_state_snapshot_t **inout_snapshot, ais_error_info_t *error)
{
    ais_state_snapshot_t *snapshot;
    if (inout_snapshot == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_snapshot_release", "missing snapshot address");
    }
    if (*inout_snapshot == NULL) {
        ais_error_clear(error);
        return AIS_OK;
    }
    snapshot = *inout_snapshot;
    for (size_t index = 0u; index < snapshot->observation_count; ++index) {
        ais_owned_observation_free(&snapshot->observations[index]);
    }
    for (size_t index = 0u; index < snapshot->percept_count; ++index) {
        ais_owned_percept_free(&snapshot->percepts[index]);
    }
    free(snapshot->observations);
    free(snapshot->percepts);
    ais_lifetime_release(snapshot->lifetime);
    free(snapshot);
    *inout_snapshot = NULL;
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_state_snapshot_observation_count(
    const ais_state_snapshot_t *snapshot, size_t *out_count,
    ais_error_info_t *error)
{
    if (snapshot == NULL || out_count == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_snapshot_observation_count", "invalid snapshot/output");
    }
    if (ais_lifetime_closed(snapshot->lifetime)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "state_snapshot_observation_count", "context is closed");
    }
    *out_count = snapshot->observation_count;
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_state_snapshot_percept_count(
    const ais_state_snapshot_t *snapshot, size_t *out_count,
    ais_error_info_t *error)
{
    if (snapshot == NULL || out_count == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_snapshot_percept_count", "invalid snapshot/output");
    }
    if (ais_lifetime_closed(snapshot->lifetime)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "state_snapshot_percept_count", "context is closed");
    }
    *out_count = snapshot->percept_count;
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_state_snapshot_observation_at(
    const ais_state_snapshot_t *snapshot, size_t index,
    ais_observation_snapshot_t **out_observation,
    ais_error_info_t *error)
{
    ais_observation_snapshot_t *result;
    if (out_observation == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_snapshot_observation_at", "missing output");
    }
    *out_observation = NULL;
    if (snapshot == NULL || index >= snapshot->observation_count) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_snapshot_observation_at", "index is out of range");
    }
    if (ais_lifetime_closed(snapshot->lifetime)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "state_snapshot_observation_at", "context is closed");
    }
    result = (ais_observation_snapshot_t *)calloc(1u, sizeof(*result));
    if (result == NULL ||
        !ais_owned_observation_clone(
            result != NULL ? &result->observation : NULL,
            &snapshot->observations[index])) {
        free(result);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "state_snapshot_observation_at", "observation allocation failed");
    }
    result->lifetime = snapshot->lifetime;
    ais_lifetime_retain(result->lifetime);
    *out_observation = result;
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_percept_snapshot_get_view(
    const ais_percept_snapshot_t *snapshot,
    ais_percept_view_t *out_view, ais_error_info_t *error)
{
    if (snapshot == NULL || out_view == NULL ||
        !ais_header_valid(
            out_view != NULL ? &out_view->header : NULL,
            sizeof(*out_view))) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "percept_snapshot_view", "invalid snapshot or output");
    }
    if (ais_lifetime_closed(snapshot->lifetime)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "percept_snapshot_view", "context is closed");
    }
    ais_percept_view_init(out_view);
    out_view->type = ais_owned_string_view(&snapshot->percept.type);
    ais_payload_to_view(&snapshot->percept.payload, &out_view->payload);
    out_view->source_observation_id =
        ais_owned_string_view(&snapshot->percept.source_observation_id);
    out_view->rule_or_model =
        ais_owned_string_view(&snapshot->percept.rule_or_model);
    out_view->validity = snapshot->percept.validity;
    ais_quality_to_view(&snapshot->percept.quality, &out_view->quality);
    ais_timestamp_to_view(
        &snapshot->percept.generated_at, &out_view->generated_at);
    out_view->source_type =
        ais_owned_string_view(&snapshot->percept.source_type);
    out_view->source_provider_id = snapshot->percept.source_provider_id;
    out_view->source_sensor_id = snapshot->percept.source_sensor_id;
    out_view->source_session_id =
        ais_owned_string_view(&snapshot->percept.source_session_id);
    out_view->source_schema =
        ais_owned_string_view(&snapshot->percept.source_schema);
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_state_snapshot_percept_at(
    const ais_state_snapshot_t *snapshot, size_t index,
    ais_percept_snapshot_t **out_percept, ais_error_info_t *error)
{
    ais_percept_snapshot_t *result;
    if (out_percept == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_snapshot_percept_at", "missing output");
    }
    *out_percept = NULL;
    if (snapshot == NULL || index >= snapshot->percept_count) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "state_snapshot_percept_at", "index is out of range");
    }
    if (ais_lifetime_closed(snapshot->lifetime)) {
        return ais_error_set(
            error, AIS_E_INVALID_STATE, 0u, NULL, NULL,
            "state_snapshot_percept_at", "context is closed");
    }
    result = (ais_percept_snapshot_t *)calloc(1u, sizeof(*result));
    if (result == NULL ||
        !ais_owned_percept_clone(
            result != NULL ? &result->percept : NULL,
            &snapshot->percepts[index])) {
        free(result);
        return ais_error_set(
            error, AIS_E_OUT_OF_MEMORY, 1u, NULL, NULL,
            "state_snapshot_percept_at", "percept allocation failed");
    }
    result->lifetime = snapshot->lifetime;
    ais_lifetime_retain(result->lifetime);
    *out_percept = result;
    ais_error_clear(error);
    return AIS_OK;
}

AIS_API ais_status_t ais_percept_snapshot_release(
    ais_percept_snapshot_t **inout_percept, ais_error_info_t *error)
{
    ais_percept_snapshot_t *snapshot;
    if (inout_percept == NULL) {
        return ais_error_set(
            error, AIS_E_INVALID_ARGUMENT, 0u, NULL, NULL,
            "percept_snapshot_release", "missing snapshot address");
    }
    if (*inout_percept == NULL) {
        ais_error_clear(error);
        return AIS_OK;
    }
    snapshot = *inout_percept;
    ais_owned_percept_free(&snapshot->percept);
    ais_lifetime_release(snapshot->lifetime);
    free(snapshot);
    *inout_percept = NULL;
    ais_error_clear(error);
    return AIS_OK;
}

static void ais_owned_percept_free(ais_owned_percept_t *percept)
{
    if (percept == NULL) {
        return;
    }
    ais_owned_string_free(&percept->type);
    ais_owned_payload_free(&percept->payload);
    ais_owned_string_free(&percept->source_observation_id);
    ais_owned_string_free(&percept->rule_or_model);
    ais_owned_timestamp_free(&percept->generated_at);
    ais_owned_string_free(&percept->source_type);
    ais_owned_string_free(&percept->source_session_id);
    ais_owned_string_free(&percept->source_schema);
    memset(percept, 0, sizeof(*percept));
}

static int ais_owned_percept_clone(
    ais_owned_percept_t *target, const ais_owned_percept_t *source)
{
    if (target == NULL || source == NULL) {
        return 0;
    }
    ais_owned_percept_free(target);
    target->validity = source->validity;
    target->quality = source->quality;
    target->source_provider_id = source->source_provider_id;
    target->source_sensor_id = source->source_sensor_id;
    if (!ais_owned_string_clone(&target->type, &source->type) ||
        !ais_owned_payload_clone(&target->payload, &source->payload) ||
        !ais_owned_string_clone(
            &target->source_observation_id, &source->source_observation_id) ||
        !ais_owned_string_clone(
            &target->rule_or_model, &source->rule_or_model) ||
        !ais_owned_timestamp_clone(
            &target->generated_at, &source->generated_at) ||
        !ais_owned_string_clone(&target->source_type, &source->source_type) ||
        !ais_owned_string_clone(
            &target->source_session_id, &source->source_session_id) ||
        !ais_owned_string_clone(&target->source_schema, &source->source_schema)) {
        ais_owned_percept_free(target);
        return 0;
    }
    return 1;
}

static ais_lifetime_t *ais_lifetime_create(void)
{
    ais_lifetime_t *lifetime = (ais_lifetime_t *)malloc(sizeof(*lifetime));
    if (lifetime == NULL) {
        return NULL;
    }
    atomic_init(&lifetime->references, 1u);
    atomic_init(&lifetime->closed, false);
    return lifetime;
}

static void ais_lifetime_retain(ais_lifetime_t *lifetime)
{
    if (lifetime != NULL) {
        (void)atomic_fetch_add_explicit(
            &lifetime->references, 1u, memory_order_relaxed);
    }
}

static void ais_lifetime_release(ais_lifetime_t *lifetime)
{
    if (lifetime != NULL &&
        atomic_fetch_sub_explicit(
            &lifetime->references, 1u, memory_order_acq_rel) == 1u) {
        free(lifetime);
    }
}

static uint8_t ais_lifetime_closed(const ais_lifetime_t *lifetime)
{
    return lifetime == NULL ||
        atomic_load_explicit(&lifetime->closed, memory_order_acquire);
}

static double ais_monotonic_seconds(void)
{
    return aisthra_platform_monotonic_seconds();
}

static int ais_now_timestamp(ais_owned_timestamp_t *timestamp)
{
    ais_timestamp_t view;
    double value = ais_monotonic_seconds();
    if (value < 0.0) {
        return 0;
    }
    ais_timestamp_init(&view);
    view.value_seconds = value;
    view.domain = AIS_CLOCK_PROCESS_MONOTONIC;
    view.clock_id = AIS_SV("aisthra.process.monotonic");
    view.present = 1u;
    view.reliable = 1u;
    return ais_owned_timestamp_copy(timestamp, &view);
}

static uint8_t ais_timestamps_comparable(
    const ais_owned_timestamp_t *left, const ais_owned_timestamp_t *right)
{
    return left != NULL && right != NULL && left->present && right->present &&
        left->reliable && right->reliable &&
        left->domain == right->domain &&
        left->clock_id.size == right->clock_id.size &&
        (left->clock_id.size == 0u ||
         memcmp(
             left->clock_id.data, right->clock_id.data,
             left->clock_id.size) == 0);
}

static uint64_t ais_age_nanoseconds(
    const ais_owned_timestamp_t *now,
    const ais_owned_timestamp_t *received)
{
    double difference;
    if (!ais_timestamps_comparable(now, received) ||
        now->value_seconds < received->value_seconds) {
        return 0u;
    }
    difference = now->value_seconds - received->value_seconds;
    if (difference <= 0.0 ||
        difference >= (double)UINT64_MAX / 1000000000.0) {
        return difference <= 0.0 ? 0u : UINT64_MAX;
    }
    return (uint64_t)(difference * 1000000000.0);
}

static void ais_error_clear(ais_error_info_t *error)
{
    if (error == NULL) {
        return;
    }
    ais_header_init(&error->header, sizeof(*error));
    error->code = AIS_OK;
    error->retryable = 0u;
    error->provider_key = (ais_string_view_t){ NULL, 0u };
    error->sensor_key = (ais_string_view_t){ NULL, 0u };
    error->operation = (ais_string_view_t){ NULL, 0u };
    error->message = (ais_string_view_t){ NULL, 0u };
}

static ais_status_t ais_error_set(
    ais_error_info_t *error, ais_status_t status, uint8_t retryable,
    const char *provider, const char *sensor, const char *operation,
    const char *message)
{
    if (error != NULL) {
        ais_header_init(&error->header, sizeof(*error));
        error->code = status;
        error->retryable = retryable;
        error->provider_key = (ais_string_view_t){
            provider != NULL ? provider : "",
            provider != NULL ? strlen(provider) : 0u
        };
        error->sensor_key = (ais_string_view_t){
            sensor != NULL ? sensor : "",
            sensor != NULL ? strlen(sensor) : 0u
        };
        error->operation = (ais_string_view_t){
            operation != NULL ? operation : "",
            operation != NULL ? strlen(operation) : 0u
        };
        error->message = (ais_string_view_t){
            message != NULL ? message : "",
            message != NULL ? strlen(message) : 0u
        };
    }
    return status;
}

static ais_status_t ais_error_set_entry(
    ais_error_info_t *error, ais_status_t status, uint8_t retryable,
    const ais_provider_entry_t *provider, const ais_sensor_entry_t *sensor,
    const char *operation, const char *message)
{
    return ais_error_set(
        error, status, retryable,
        provider != NULL ? provider->key.data : NULL,
        sensor != NULL ? sensor->key.data : NULL, operation, message);
}

static const char *ais_status_message(ais_status_t status)
{
    switch (status) {
    case AIS_OK: return "ok";
    case AIS_E_INVALID_ARGUMENT: return "invalid argument";
    case AIS_E_OUT_OF_MEMORY: return "out of memory";
    case AIS_E_UNKNOWN_SENSOR: return "unknown sensor";
    case AIS_E_UNKNOWN_PROVIDER: return "unknown provider";
    case AIS_E_NO_DATA: return "no data";
    case AIS_E_TYPE_MISMATCH: return "type mismatch";
    case AIS_E_STALE: return "stale observation";
    case AIS_E_TIMESTAMP_UNCOMPARABLE: return "timestamps incomparable";
    case AIS_E_INVALID_OBSERVATION: return "invalid observation";
    case AIS_E_DUPLICATE: return "duplicate observation";
    case AIS_E_PROVIDER_UNAVAILABLE: return "provider unavailable";
    case AIS_E_INVALID_STATE: return "invalid state";
    case AIS_E_SCHEMA_MISMATCH: return "schema mismatch";
    case AIS_E_UNSUPPORTED: return "unsupported";
    case AIS_E_BUSY: return "busy";
    case AIS_E_TIMEOUT: return "timeout";
    case AIS_E_CANCELLED: return "cancelled";
    case AIS_E_BUFFER_TOO_SMALL: return "buffer too small";
    case AIS_E_PERMISSION: return "permission denied";
    case AIS_E_CONFLICT: return "conflict";
    case AIS_E_INTERNAL: return "internal error";
    default: return "unknown status";
    }
}

AIS_API const char *ais_status_name(ais_status_t status)
{
    return ais_status_message(status);
}

AIS_API uint8_t ais_status_is_retryable(ais_status_t status)
{
    switch (status) {
    case AIS_E_OUT_OF_MEMORY:
    case AIS_E_PROVIDER_UNAVAILABLE:
    case AIS_E_BUSY:
    case AIS_E_TIMEOUT:
    case AIS_E_BUFFER_TOO_SMALL:
        return 1u;
    default:
        return 0u;
    }
}

AIS_API void ais_error_info_init(ais_error_info_t *error)
{
    ais_error_clear(error);
}

AIS_API ais_string_view_t ais_string_view_make(
    const char *data, size_t size)
{
    return (ais_string_view_t){ data, size };
}

AIS_API ais_string_view_t ais_string_view_from_cstr(const char *data)
{
    return data == NULL ?
        (ais_string_view_t){ NULL, 0u } :
        (ais_string_view_t){ data, strlen(data) };
}

AIS_API void ais_timestamp_init(ais_timestamp_t *timestamp)
{
    if (timestamp == NULL) {
        return;
    }
    memset(timestamp, 0, sizeof(*timestamp));
    ais_header_init(&timestamp->header, sizeof(*timestamp));
    timestamp->domain = AIS_CLOCK_UNKNOWN;
}

AIS_API void ais_quality_init(ais_quality_t *quality)
{
    if (quality == NULL) {
        return;
    }
    memset(quality, 0, sizeof(*quality));
    ais_header_init(&quality->header, sizeof(*quality));
}

AIS_API void ais_provenance_init(ais_provenance_t *provenance)
{
    if (provenance == NULL) {
        return;
    }
    memset(provenance, 0, sizeof(*provenance));
    ais_header_init(&provenance->header, sizeof(*provenance));
}

AIS_API void ais_payload_init(ais_payload_view_t *payload)
{
    if (payload == NULL) {
        return;
    }
    memset(payload, 0, sizeof(*payload));
    ais_header_init(&payload->header, sizeof(*payload));
    payload->kind = AIS_PAYLOAD_NONE;
}

AIS_API ais_status_t ais_timestamp_set(
    ais_timestamp_t *timestamp, double value_seconds, uint32_t domain,
    ais_string_view_t clock_id, uint8_t reliable)
{
    if (timestamp == NULL || !ais_text_valid(clock_id, 1u) ||
        !ais_double_valid(value_seconds) || !ais_domain_valid(domain) ||
        reliable > 1u) {
        return AIS_E_INVALID_ARGUMENT;
    }
    ais_timestamp_init(timestamp);
    timestamp->value_seconds = value_seconds;
    timestamp->domain = domain;
    timestamp->clock_id = clock_id;
    timestamp->present = 1u;
    timestamp->reliable = reliable;
    return AIS_OK;
}

static ais_status_t ais_payload_prepare(
    ais_payload_view_t *payload, uint32_t kind)
{
    if (payload == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    ais_payload_init(payload);
    payload->kind = kind;
    return AIS_OK;
}

AIS_API ais_status_t ais_payload_set_bool(
    ais_payload_view_t *payload, uint8_t value)
{
    ais_status_t status = ais_payload_prepare(payload, AIS_PAYLOAD_BOOL);
    if (status == AIS_OK) {
        if (value > 1u) {
            return AIS_E_INVALID_ARGUMENT;
        }
        payload->as.bool_value = value;
    }
    return status;
}

AIS_API ais_status_t ais_payload_set_int(
    ais_payload_view_t *payload, int64_t value)
{
    ais_status_t status = ais_payload_prepare(payload, AIS_PAYLOAD_INT);
    if (status == AIS_OK) {
        payload->as.int_value = value;
    }
    return status;
}

AIS_API ais_status_t ais_payload_set_real(
    ais_payload_view_t *payload, double value)
{
    ais_status_t status = ais_payload_prepare(payload, AIS_PAYLOAD_REAL);
    if (status == AIS_OK && !ais_double_valid(value)) {
        return AIS_E_INVALID_ARGUMENT;
    }
    if (status == AIS_OK) {
        payload->as.real_value = value;
    }
    return status;
}

AIS_API ais_status_t ais_payload_set_vector(
    ais_payload_view_t *payload, double x, double y, double z)
{
    ais_status_t status = ais_payload_prepare(payload, AIS_PAYLOAD_VECTOR);
    if (status == AIS_OK &&
        (!ais_double_valid(x) || !ais_double_valid(y) ||
         !ais_double_valid(z))) {
        return AIS_E_INVALID_ARGUMENT;
    }
    if (status == AIS_OK) {
        payload->as.vector = (ais_vector_value_t){ x, y, z };
    }
    return status;
}

AIS_API ais_status_t ais_payload_set_event(
    ais_payload_view_t *payload, ais_string_view_t name,
    ais_string_view_t value)
{
    ais_status_t status = ais_payload_prepare(payload, AIS_PAYLOAD_EVENT);
    if (status == AIS_OK &&
        (!ais_text_valid(name, 1u) || !ais_text_valid(value, 0u))) {
        return AIS_E_INVALID_ARGUMENT;
    }
    if (status == AIS_OK) {
        payload->as.event.name = name;
        payload->as.event.value = value;
    }
    return status;
}

AIS_API ais_status_t ais_payload_set_structure(
    ais_payload_view_t *payload, ais_string_view_t label,
    double x, double y, double width, double height, ais_string_view_t aux)
{
    ais_status_t status = ais_payload_prepare(payload, AIS_PAYLOAD_STRUCT);
    if (status == AIS_OK &&
        (!ais_text_valid(label, 0u) || !ais_text_valid(aux, 0u) ||
         !ais_double_valid(x) || !ais_double_valid(y) ||
         !ais_double_valid(width) || !ais_double_valid(height))) {
        return AIS_E_INVALID_ARGUMENT;
    }
    if (status == AIS_OK) {
        payload->as.structure.label = label;
        payload->as.structure.x = x;
        payload->as.structure.y = y;
        payload->as.structure.width = width;
        payload->as.structure.height = height;
        payload->as.structure.aux = aux;
    }
    return status;
}

AIS_API ais_status_t ais_payload_set_bytes(
    ais_payload_view_t *payload, ais_bytes_view_t bytes)
{
    ais_status_t status = ais_payload_prepare(payload, AIS_PAYLOAD_BYTES);
    if (status == AIS_OK && !ais_bytes_valid(bytes)) {
        return AIS_E_INVALID_ARGUMENT;
    }
    if (status == AIS_OK) {
        payload->as.bytes = bytes;
    }
    return status;
}

AIS_API void ais_ingest_result_init(ais_ingest_result_t *result)
{
    if (result == NULL) {
        return;
    }
    memset(result, 0, sizeof(*result));
    ais_header_init(&result->header, sizeof(*result));
    result->disposition = AIS_INGEST_REJECTED;
}

AIS_API void ais_observation_view_init(ais_observation_view_t *observation)
{
    if (observation == NULL) {
        return;
    }
    memset(observation, 0, sizeof(*observation));
    ais_header_init(&observation->header, sizeof(*observation));
    ais_timestamp_init(&observation->acquired_at);
    ais_timestamp_init(&observation->received_at);
    ais_payload_init(&observation->payload);
    ais_quality_init(&observation->quality);
    ais_provenance_init(&observation->provenance);
    observation->validity = AIS_VALIDITY_VALID;
}

AIS_API void ais_state_summary_init(ais_state_summary_t *summary)
{
    if (summary == NULL) {
        return;
    }
    memset(summary, 0, sizeof(*summary));
    ais_header_init(&summary->header, sizeof(*summary));
}

AIS_API void ais_percept_view_init(ais_percept_view_t *view)
{
    if (view == NULL) {
        return;
    }
    memset(view, 0, sizeof(*view));
    ais_header_init(&view->header, sizeof(*view));
    ais_timestamp_init(&view->generated_at);
    ais_quality_init(&view->quality);
    ais_payload_init(&view->payload);
}

AIS_API ais_status_t ais_library_version(
    uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    if (major == NULL || minor == NULL || patch == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    *major = 0u;
    *minor = 1u;
    *patch = 0u;
    return AIS_OK;
}

AIS_API ais_status_t ais_api_version(uint32_t *major, uint32_t *minor)
{
    if (major == NULL || minor == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    *major = 1u;
    *minor = 0u;
    return AIS_OK;
}

AIS_API ais_status_t ais_abi_version(uint32_t *major, uint32_t *minor)
{
    if (major == NULL || minor == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    *major = 1u;
    *minor = 0u;
    return AIS_OK;
}

AIS_API ais_status_t ais_schema_version(uint32_t *major, uint32_t *minor)
{
    if (major == NULL || minor == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    *major = AIS_SCHEMA_MAJOR;
    *minor = AIS_SCHEMA_MINOR;
    return AIS_OK;
}

AIS_API ais_status_t ais_feature_supported(
    uint32_t feature, uint8_t *out_supported)
{
    if (out_supported == NULL) {
        return AIS_E_INVALID_ARGUMENT;
    }
    *out_supported = feature == 1u ? 1u : 0u;
    return AIS_OK;
}

AIS_API const char *ais_build_id(void)
{
    return "aisthra-c17-0.1.0";
}

static const ais_builtin_schema_t ais_builtins[] = {
    {
        "interaction.button.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_EVENT), AIS_UNIT_NONE, "",
        AIS_REF_DEVICE, "device", AIS_ACQUISITION_EVENT,
        AIS_CAP_EVENT | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "interaction.pointer_press.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_PIXEL_LOGICAL,
        "pixel.logical", AIS_REF_QT_LOCAL_LOGICAL, "qt.local.logical",
        AIS_ACQUISITION_EVENT,
        AIS_CAP_EVENT | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "interaction.pointer_move_local.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_PIXEL_LOGICAL,
        "pixel.logical", AIS_REF_QT_LOCAL_LOGICAL, "qt.local.logical",
        AIS_ACQUISITION_STREAM,
        AIS_CAP_EVENT | AIS_CAP_STREAM | AIS_CAP_QUALITY |
            AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "interaction.pointer_release.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_PIXEL_LOGICAL,
        "pixel.logical", AIS_REF_QT_LOCAL_LOGICAL, "qt.local.logical",
        AIS_ACQUISITION_EVENT,
        AIS_CAP_EVENT | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "interaction.pointer_deactivate.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_PIXEL_LOGICAL,
        "pixel.logical", AIS_REF_QT_LOCAL_LOGICAL, "qt.local.logical",
        AIS_ACQUISITION_EVENT,
        AIS_CAP_EVENT | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "interaction.pointer_enter.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_PIXEL_LOGICAL,
        "pixel.logical", AIS_REF_QT_LOCAL_LOGICAL, "qt.local.logical",
        AIS_ACQUISITION_EVENT,
        AIS_CAP_EVENT | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "interaction.pointer_leave.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_PIXEL_LOGICAL,
        "pixel.logical", AIS_REF_QT_LOCAL_LOGICAL, "qt.local.logical",
        AIS_ACQUISITION_EVENT,
        AIS_CAP_EVENT | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "desktop.window_geometry.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_PIXEL_LOGICAL,
        "pixel.logical", AIS_REF_SWAY_OUTPUT_LOGICAL,
        "sway.output.logical", AIS_ACQUISITION_POINT,
        AIS_CAP_READ | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "desktop.workspace_snapshot.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_NONE, "",
        AIS_REF_NONE, "", AIS_ACQUISITION_POINT,
        AIS_CAP_READ | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "desktop.output_geometry.v1", AIS_MODALITY_INTERACTION,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_STRUCT) |
            AIS_PAYLOAD_MASK(AIS_PAYLOAD_BYTES), AIS_UNIT_PIXEL_LOGICAL,
        "pixel.logical", AIS_REF_SWAY_OUTPUT_LOGICAL,
        "sway.output.logical", AIS_ACQUISITION_POINT,
        AIS_CAP_READ | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "environment.temperature.v1", AIS_MODALITY_ENVIRONMENTAL,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_REAL), AIS_UNIT_CELSIUS, "celsius",
        AIS_REF_DEVICE, "device", AIS_ACQUISITION_POINT,
        AIS_CAP_READ | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "spatial.distance.v1", AIS_MODALITY_SPATIAL,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_REAL), AIS_UNIT_METER, "meter",
        AIS_REF_DEVICE, "device", AIS_ACQUISITION_POINT,
        AIS_CAP_READ | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    },
    {
        "spatial.acceleration.v1", AIS_MODALITY_SPATIAL,
        AIS_PAYLOAD_MASK(AIS_PAYLOAD_REAL), AIS_UNIT_NONE_DIMENSIONAL,
        "none", AIS_REF_DEVICE, "device", AIS_ACQUISITION_POINT,
        AIS_CAP_READ | AIS_CAP_QUALITY | AIS_CAP_ACQUISITION_TIMESTAMP
    }
};

static const ais_builtin_schema_t *ais_builtin_find(ais_string_view_t id)
{
    for (size_t index = 0u;
         index < sizeof(ais_builtins) / sizeof(ais_builtins[0]);
         ++index) {
        size_t length = strlen(ais_builtins[index].id);
        if (id.size == length &&
            id.data != NULL &&
            memcmp(id.data, ais_builtins[index].id, length) == 0) {
            return &ais_builtins[index];
        }
    }
    return NULL;
}

static ais_schema_entry_t *ais_custom_schema_find(
    ais_ctx_t *ctx, ais_string_view_t id)
{
    if (ctx == NULL) {
        return NULL;
    }
    for (size_t index = 0u; index < ctx->schema_count; ++index) {
        ais_schema_entry_t *schema = ctx->schemas[index];
        if (schema->active && schema->schema_id.size == id.size &&
            id.data != NULL &&
            memcmp(schema->schema_id.data, id.data, id.size) == 0) {
            return schema;
        }
    }
    return NULL;
}

static uint8_t ais_schema_callback_begin_locked(
    ais_schema_entry_t *schema)
{
    if (schema == NULL || !schema->active ||
        schema->callbacks.validate == NULL) {
        return 0u;
    }
    schema->in_flight += 1u;
    return 1u;
}

static void ais_schema_callback_end(
    ais_ctx_t *ctx, ais_schema_entry_t *schema)
{
    if (ctx == NULL || schema == NULL) {
        return;
    }
    (void)mtx_lock(&ctx->mutex);
    if (schema->in_flight > 0u) {
        schema->in_flight -= 1u;
    }
    (void)cnd_broadcast(&ctx->condition);
    (void)mtx_unlock(&ctx->mutex);
}

static ais_provider_entry_t *ais_provider_find(
    ais_ctx_t *ctx, ais_provider_id_t id)
{
    if (ctx == NULL || id == 0u) {
        return NULL;
    }
    for (size_t index = 0u; index < ctx->provider_count; ++index) {
        if (ctx->providers[index].id == id) {
            return &ctx->providers[index];
        }
    }
    return NULL;
}

static ais_sensor_entry_t *ais_sensor_find(
    ais_ctx_t *ctx, ais_sensor_id_t id)
{
    if (ctx == NULL || id == 0u) {
        return NULL;
    }
    for (size_t index = 0u; index < ctx->sensor_count; ++index) {
        if (ctx->sensors[index].id == id) {
            return &ctx->sensors[index];
        }
    }
    return NULL;
}

#if defined(AISTHRA_TESTING)
void ais_test_set_sensor_sequence(
    ais_ctx_t *ctx, ais_sensor_id_t sensor_id, uint64_t next_sequence)
{
    ais_sensor_entry_t *sensor;
    if (ctx == NULL || !ais_ctx_enter(ctx)) {
        return;
    }
    (void)mtx_lock(&ctx->mutex);
    sensor = ais_sensor_find(ctx, sensor_id);
    if (sensor != NULL) {
        sensor->next_sequence = next_sequence;
        sensor->sequence_exhausted = 0u;
    }
    (void)mtx_unlock(&ctx->mutex);
    ais_ctx_leave(ctx);
}
#endif

static uint32_t ais_unit_code(ais_string_view_t value)
{
    if (value.size == 0u) {
        return AIS_UNIT_NONE;
    }
    if (value.size == 5u && memcmp(value.data, "meter", 5u) == 0) {
        return AIS_UNIT_METER;
    }
    if (value.size == 1u && value.data[0] == 'm') {
        return AIS_UNIT_METER;
    }
    if (value.size == 7u && memcmp(value.data, "celsius", 7u) == 0) {
        return AIS_UNIT_CELSIUS;
    }
    if (value.size == 7u && memcmp(value.data, "percent", 7u) == 0) {
        return AIS_UNIT_PERCENT;
    }
    if (value.size == 3u && memcmp(value.data, "lux", 3u) == 0) {
        return AIS_UNIT_NONE_DIMENSIONAL;
    }
    if (value.size == 13u &&
        memcmp(value.data, "pixel.logical", 13u) == 0) {
        return AIS_UNIT_PIXEL_LOGICAL;
    }
    if (value.size == 4u && memcmp(value.data, "none", 4u) == 0) {
        return AIS_UNIT_NONE_DIMENSIONAL;
    }
    return AIS_UNIT_NONE;
}

static uint32_t ais_reference_code(ais_string_view_t value)
{
    if (value.size == 0u) {
        return AIS_REF_NONE;
    }
    if (value.size == 6u && memcmp(value.data, "device", 6u) == 0) {
        return AIS_REF_DEVICE;
    }
    if (value.size == 16u &&
        memcmp(value.data, "qt.local.logical", 16u) == 0) {
        return AIS_REF_QT_LOCAL_LOGICAL;
    }
    if (value.size == 19u &&
        memcmp(value.data, "sway.output.logical", 19u) == 0) {
        return AIS_REF_SWAY_OUTPUT_LOGICAL;
    }
    if (value.size == 5u && memcmp(value.data, "world", 5u) == 0) {
        return AIS_REF_WORLD;
    }
    return AIS_REF_NONE;
}

static int ais_string_equal(
    const ais_owned_string_t *left, ais_string_view_t right)
{
    return left != NULL && left->size == right.size &&
        (right.size == 0u ||
         (right.data != NULL &&
          memcmp(left->data, right.data, right.size) == 0));
}

static int ais_owned_string_equal(
    const ais_owned_string_t *left, const ais_owned_string_t *right)
{
    return left != NULL && right != NULL && left->size == right->size &&
        (left->size == 0u ||
         memcmp(left->data, right->data, left->size) == 0);
}

static int ais_text_valid(ais_string_view_t value, uint8_t required)
{
    if (value.size == 0u) {
        return required == 0u;
    }
    if (value.data == NULL || value.size > SIZE_MAX - 1u ||
        !ais_utf8_valid(value)) {
        return 0;
    }
    for (size_t index = 0u; index < value.size; ++index) {
        if (value.data[index] == '\0') {
            return 0;
        }
    }
    return 1;
}

static int ais_bytes_valid(ais_bytes_view_t value)
{
    return value.size == 0u || value.data != NULL;
}

/* AIP1 is a little-endian, fixed-size wire payload. It is deliberately
 * encoded field-by-field by the bridge; no C struct layout is part of it. */
static int ais_pointer_event_bytes_valid(
    ais_bytes_view_t value, uint32_t expected_event_type)
{
    const uint8_t *bytes = (const uint8_t *)value.data;
    uint32_t event_type;
    uint16_t version;
    uint16_t header_size;
    double x;
    double y;
    if (!ais_bytes_valid(value) || value.size != 36u || bytes == NULL) {
        return 0;
    }
    if (memcmp(bytes, "AIP1", 4u) != 0) {
        return 0;
    }
    version = (uint16_t)bytes[4] | (uint16_t)bytes[5] << 8u;
    header_size = (uint16_t)bytes[6] | (uint16_t)bytes[7] << 8u;
    if (version != 1u || header_size != 36u) {
        return 0;
    }
    event_type = (uint32_t)bytes[8] |
        (uint32_t)bytes[9] << 8u |
        (uint32_t)bytes[10] << 16u |
        (uint32_t)bytes[11] << 24u;
    if (event_type != expected_event_type) {
        return 0;
    }
    {
        uint16_t marker = 1u;
        unsigned char raw[sizeof(double)];
        if (*(unsigned char *)&marker == 1u) {
            memcpy(raw, bytes + 20u, sizeof(raw));
            memcpy(&x, raw, sizeof(x));
            memcpy(raw, bytes + 28u, sizeof(raw));
            memcpy(&y, raw, sizeof(y));
        } else {
            for (size_t index = 0u; index < sizeof(raw); ++index) {
                raw[sizeof(raw) - 1u - index] = bytes[20u + index];
            }
            memcpy(&x, raw, sizeof(x));
            for (size_t index = 0u; index < sizeof(raw); ++index) {
                raw[sizeof(raw) - 1u - index] = bytes[28u + index];
            }
            memcpy(&y, raw, sizeof(y));
        }
    }
    return ais_double_valid(x) && ais_double_valid(y);
}

/* ASD1 is the private, field-by-field wire format for desktop snapshots.
 * The fixed header is 80 bytes; strings follow as UTF-8 length-delimited data. */
static int ais_desktop_bytes_valid(
    ais_bytes_view_t value, uint32_t expected_kind)
{
    const uint8_t *bytes = (const uint8_t *)value.data;
    uint32_t size;
    uint16_t version;
    uint16_t kind;
    uint16_t lengths[3];
    size_t cursor = 80u;
    if (!ais_bytes_valid(value) || bytes == NULL || value.size < 80u ||
        memcmp(bytes, "ASD1", 4u) != 0) return 0;
    version = (uint16_t)bytes[4] | (uint16_t)bytes[5] << 8u;
    kind = (uint16_t)bytes[6] | (uint16_t)bytes[7] << 8u;
    size = (uint32_t)bytes[8] | (uint32_t)bytes[9] << 8u |
        (uint32_t)bytes[10] << 16u | (uint32_t)bytes[11] << 24u;
    if (version != 1u || kind != expected_kind || size != value.size) return 0;
    for (size_t index = 0u; index < 3u; ++index) {
        lengths[index] = (uint16_t)bytes[72u + index * 2u] |
            (uint16_t)bytes[73u + index * 2u] << 8u;
        if (cursor > value.size - lengths[index]) return 0;
        if (!ais_utf8_valid((ais_string_view_t){
                (const char *)bytes + cursor, lengths[index] })) return 0;
        cursor += lengths[index];
    }
    if (cursor != value.size) return 0;
    {
        uint16_t marker = 1u;
        unsigned char raw[sizeof(double)];
        double values[5];
        for (size_t value_index = 0u; value_index < 5u; ++value_index) {
            const uint8_t *source = bytes + 32u + value_index * 8u;
            if (*(unsigned char *)&marker == 1u) {
                memcpy(raw, source, sizeof(raw));
            } else {
                for (size_t byte_index = 0u; byte_index < sizeof(raw); ++byte_index)
                    raw[sizeof(raw) - 1u - byte_index] = source[byte_index];
            }
            memcpy(&values[value_index], raw, sizeof(values[value_index]));
            if (!ais_double_valid(values[value_index])) return 0;
        }
    }
    return 1;
}

static int ais_payload_view_well_formed(
    const ais_payload_view_t *payload, size_t max_bytes)
{
    if (payload == NULL ||
        !ais_header_valid(&payload->header, sizeof(*payload))) {
        return 0;
    }
    switch (payload->kind) {
    case AIS_PAYLOAD_NONE:
    case AIS_PAYLOAD_IMAGE_FRAME:
    case AIS_PAYLOAD_AUDIO_BLOCK:
    case AIS_PAYLOAD_REFERENCE:
        return 1;
    case AIS_PAYLOAD_BOOL:
        return payload->as.bool_value <= 1u;
    case AIS_PAYLOAD_INT:
        return 1;
    case AIS_PAYLOAD_REAL:
        return ais_double_valid(payload->as.real_value);
    case AIS_PAYLOAD_VECTOR:
        return ais_double_valid(payload->as.vector.x) &&
            ais_double_valid(payload->as.vector.y) &&
            ais_double_valid(payload->as.vector.z);
    case AIS_PAYLOAD_EVENT:
        return ais_text_valid(payload->as.event.name, 1u) &&
            ais_text_valid(payload->as.event.value, 0u);
    case AIS_PAYLOAD_STRUCT:
        return ais_text_valid(payload->as.structure.label, 0u) &&
            ais_text_valid(payload->as.structure.aux, 0u) &&
            ais_double_valid(payload->as.structure.x) &&
            ais_double_valid(payload->as.structure.y) &&
            ais_double_valid(payload->as.structure.width) &&
            ais_double_valid(payload->as.structure.height);
    case AIS_PAYLOAD_BYTES:
        return ais_bytes_valid(payload->as.bytes) &&
            payload->as.bytes.size <= max_bytes;
    default:
        return 0;
    }
}

static int ais_array_grow(
    void **array, size_t *capacity, size_t needed, size_t element_size)
{
    size_t next;
    void *replacement;
    if (needed <= *capacity) {
        return 1;
    }
    if (element_size == 0u || needed > SIZE_MAX / element_size) {
        return 0;
    }
    next = *capacity == 0u ? 8u : *capacity;
    while (next < needed) {
        if (next > SIZE_MAX / 2u) {
            next = needed;
            break;
        }
        next *= 2u;
    }
    if (next > SIZE_MAX / element_size) {
        return 0;
    }
    replacement = realloc(*array, next * element_size);
    if (replacement == NULL) {
        return 0;
    }
    *array = replacement;
    *capacity = next;
    return 1;
}

static int ais_owned_string_set(
    ais_owned_string_t *target, ais_string_view_t source)
{
    char *copy;
    if (target == NULL || !ais_text_valid(source, 0u)) {
        return 0;
    }
    if (source.size == 0u) {
        ais_owned_string_free(target);
        return 1;
    }
    if (source.size > SIZE_MAX - 1u) {
        return 0;
    }
    copy = (char *)malloc(source.size + 1u);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, source.data, source.size);
    copy[source.size] = '\0';
    ais_owned_string_free(target);
    target->data = copy;
    target->size = source.size;
    return 1;
}

/* Internal strings have already passed the public UTF-8 and embedded-NUL
 * checks when they were created. Cloning them must still duplicate storage,
 * but does not need to validate the same bytes again. */
static int ais_owned_string_clone(
    ais_owned_string_t *target, const ais_owned_string_t *source)
{
    char *copy;
    if (target == NULL || source == NULL) {
        return 0;
    }
    if (target == source) {
        return 1;
    }
    if (source->size == 0u) {
        ais_owned_string_free(target);
        return 1;
    }
    if (source->data == NULL || source->size > SIZE_MAX - 1u) {
        return 0;
    }
    copy = (char *)malloc(source->size + 1u);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, source->data, source->size + 1u);
    ais_owned_string_free(target);
    target->data = copy;
    target->size = source->size;
    return 1;
}

static int ais_owned_string_set_literal(
    ais_owned_string_t *target, const char *source)
{
    if (source == NULL) {
        return 0;
    }
    return ais_owned_string_set(
        target, (ais_string_view_t){ source, strlen(source) });
}

static void ais_owned_string_free(ais_owned_string_t *value)
{
    if (value != NULL) {
        free(value->data);
        value->data = NULL;
        value->size = 0u;
    }
}

static ais_string_view_t ais_owned_string_view(
    const ais_owned_string_t *value)
{
    if (value == NULL || value->data == NULL) {
        return (ais_string_view_t){ NULL, 0u };
    }
    return (ais_string_view_t){ value->data, value->size };
}

static uint8_t ais_double_valid(double value)
{
    return isfinite(value) ? 1u : 0u;
}

static uint8_t ais_domain_valid(uint32_t domain)
{
    return domain >= AIS_CLOCK_PROCESS_MONOTONIC &&
        domain <= AIS_CLOCK_UNKNOWN;
}

static uint8_t ais_timestamp_valid(
    const ais_owned_timestamp_t *timestamp)
{
    if (timestamp == NULL || timestamp->present == 0u) {
        return 1u;
    }
    return ais_double_valid(timestamp->value_seconds) &&
        ais_domain_valid(timestamp->domain) &&
        timestamp->clock_id.size > 0u &&
        timestamp->reliable <= 1u;
}

static uint8_t ais_quality_valid(const ais_owned_quality_t *quality)
{
    return quality != NULL &&
        quality->known <= 1u &&
        (!quality->known ||
         (ais_double_valid(quality->score) &&
          quality->score >= 0.0 && quality->score <= 1.0)) &&
        ais_double_valid(quality->age_seconds) &&
        quality->age_seconds >= 0.0;
}

static void ais_owned_timestamp_free(ais_owned_timestamp_t *timestamp)
{
    if (timestamp != NULL) {
        ais_owned_string_free(&timestamp->clock_id);
        memset(timestamp, 0, sizeof(*timestamp));
    }
}

static int ais_owned_timestamp_copy(
    ais_owned_timestamp_t *target, const ais_timestamp_t *source)
{
    if (target == NULL || source == NULL ||
        !ais_header_valid(&source->header, sizeof(*source)) ||
        !ais_text_valid(source->clock_id, 0u)) {
        return 0;
    }
    ais_owned_timestamp_free(target);
    target->value_seconds = source->value_seconds;
    target->domain = source->domain;
    target->present = source->present;
    target->reliable = source->reliable;
    if (!ais_owned_string_set(&target->clock_id, source->clock_id)) {
        ais_owned_timestamp_free(target);
        return 0;
    }
    return ais_timestamp_valid(target);
}

static int ais_owned_timestamp_clone(
    ais_owned_timestamp_t *target, const ais_owned_timestamp_t *source)
{
    if (target == NULL || source == NULL) {
        return 0;
    }
    ais_owned_timestamp_free(target);
    target->value_seconds = source->value_seconds;
    target->domain = source->domain;
    target->present = source->present;
    target->reliable = source->reliable;
    if (!ais_owned_string_clone(&target->clock_id, &source->clock_id)) {
        ais_owned_timestamp_free(target);
        return 0;
    }
    return 1;
}

static void ais_owned_provenance_free(ais_owned_provenance_t *provenance)
{
    if (provenance != NULL) {
        ais_owned_string_free(&provenance->source_type);
        ais_owned_string_free(&provenance->provider_version);
        ais_owned_string_free(&provenance->rule_or_model);
        ais_owned_string_free(&provenance->source_observation_id);
        memset(provenance, 0, sizeof(*provenance));
    }
}

static int ais_owned_provenance_copy(
    ais_owned_provenance_t *target, const ais_provenance_t *source)
{
    if (target == NULL || source == NULL ||
        !ais_header_valid(&source->header, sizeof(*source)) ||
        !ais_text_valid(source->source_type, 1u) ||
        !ais_text_valid(source->provider_version, 0u) ||
        !ais_text_valid(source->rule_or_model, 0u) ||
        !ais_text_valid(source->source_observation_id, 0u)) {
        return 0;
    }
    ais_owned_provenance_free(target);
    if (!ais_owned_string_set(&target->source_type, source->source_type) ||
        !ais_owned_string_set(
            &target->provider_version, source->provider_version) ||
        !ais_owned_string_set(
            &target->rule_or_model, source->rule_or_model) ||
        !ais_owned_string_set(
            &target->source_observation_id, source->source_observation_id)) {
        ais_owned_provenance_free(target);
        return 0;
    }
    return 1;
}

static int ais_owned_provenance_clone(
    ais_owned_provenance_t *target, const ais_owned_provenance_t *source)
{
    if (target == NULL || source == NULL) {
        return 0;
    }
    ais_owned_provenance_free(target);
    if (!ais_owned_string_clone(&target->source_type, &source->source_type) ||
        !ais_owned_string_clone(
            &target->provider_version, &source->provider_version) ||
        !ais_owned_string_clone(
            &target->rule_or_model, &source->rule_or_model) ||
        !ais_owned_string_clone(
            &target->source_observation_id, &source->source_observation_id)) {
        ais_owned_provenance_free(target);
        return 0;
    }
    return 1;
}

static void ais_owned_payload_free(ais_owned_payload_t *payload)
{
    if (payload == NULL) {
        return;
    }
    if (payload->kind == AIS_PAYLOAD_EVENT) {
        ais_owned_string_free(&payload->as.event.name);
        ais_owned_string_free(&payload->as.event.value);
    } else if (payload->kind == AIS_PAYLOAD_STRUCT) {
        ais_owned_string_free(&payload->as.structure.label);
        ais_owned_string_free(&payload->as.structure.aux);
    } else if (payload->kind == AIS_PAYLOAD_BYTES) {
        free(payload->as.bytes.data);
    }
    memset(payload, 0, sizeof(*payload));
}

static int ais_owned_payload_copy(
    ais_owned_payload_t *target, const ais_payload_view_t *source,
    size_t max_bytes)
{
    if (target == NULL || source == NULL ||
        !ais_header_valid(&source->header, sizeof(*source))) {
        return 0;
    }
    ais_owned_payload_free(target);
    target->kind = source->kind;
    switch (source->kind) {
    case AIS_PAYLOAD_NONE:
    case AIS_PAYLOAD_IMAGE_FRAME:
    case AIS_PAYLOAD_AUDIO_BLOCK:
    case AIS_PAYLOAD_REFERENCE:
        return 1;
    case AIS_PAYLOAD_BOOL:
        if (source->as.bool_value > 1u) {
            ais_owned_payload_free(target);
            return 0;
        }
        target->as.bool_value = source->as.bool_value;
        return 1;
    case AIS_PAYLOAD_INT:
        target->as.int_value = source->as.int_value;
        return 1;
    case AIS_PAYLOAD_REAL:
        target->as.real_value = source->as.real_value;
        return ais_double_valid(source->as.real_value);
    case AIS_PAYLOAD_VECTOR:
        target->as.vector = source->as.vector;
        return ais_double_valid(source->as.vector.x) &&
            ais_double_valid(source->as.vector.y) &&
            ais_double_valid(source->as.vector.z);
    case AIS_PAYLOAD_EVENT:
        if (!ais_text_valid(source->as.event.name, 1u) ||
            !ais_text_valid(source->as.event.value, 0u) ||
            !ais_owned_string_set(
                &target->as.event.name, source->as.event.name) ||
            !ais_owned_string_set(
                &target->as.event.value, source->as.event.value)) {
            ais_owned_payload_free(target);
            return 0;
        }
        return 1;
    case AIS_PAYLOAD_STRUCT:
        if (!ais_text_valid(source->as.structure.label, 0u) ||
            !ais_text_valid(source->as.structure.aux, 0u) ||
            !ais_double_valid(source->as.structure.x) ||
            !ais_double_valid(source->as.structure.y) ||
            !ais_double_valid(source->as.structure.width) ||
            !ais_double_valid(source->as.structure.height) ||
            !ais_owned_string_set(
                &target->as.structure.label, source->as.structure.label) ||
            !ais_owned_string_set(
                &target->as.structure.aux, source->as.structure.aux)) {
            ais_owned_payload_free(target);
            return 0;
        }
        target->as.structure.x = source->as.structure.x;
        target->as.structure.y = source->as.structure.y;
        target->as.structure.width = source->as.structure.width;
        target->as.structure.height = source->as.structure.height;
        return 1;
    case AIS_PAYLOAD_BYTES:
        if (!ais_bytes_valid(source->as.bytes) ||
            source->as.bytes.size > max_bytes) {
            ais_owned_payload_free(target);
            return 0;
        }
        if (source->as.bytes.size > 0u) {
            target->as.bytes.data = malloc(source->as.bytes.size);
            if (target->as.bytes.data == NULL) {
                ais_owned_payload_free(target);
                return 0;
            }
            memcpy(
                target->as.bytes.data, source->as.bytes.data,
                source->as.bytes.size);
        }
        target->as.bytes.size = source->as.bytes.size;
        return 1;
    default:
        ais_owned_payload_free(target);
        return 0;
    }
}

static int ais_owned_payload_clone(
    ais_owned_payload_t *target, const ais_owned_payload_t *source)
{
    if (target == NULL || source == NULL) {
        return 0;
    }
    ais_owned_payload_free(target);
    target->kind = source->kind;
    switch (source->kind) {
    case AIS_PAYLOAD_NONE:
    case AIS_PAYLOAD_IMAGE_FRAME:
    case AIS_PAYLOAD_AUDIO_BLOCK:
    case AIS_PAYLOAD_REFERENCE:
        return 1;
    case AIS_PAYLOAD_BOOL:
        target->as.bool_value = source->as.bool_value;
        return 1;
    case AIS_PAYLOAD_INT:
        target->as.int_value = source->as.int_value;
        return 1;
    case AIS_PAYLOAD_REAL:
        target->as.real_value = source->as.real_value;
        return 1;
    case AIS_PAYLOAD_VECTOR:
        target->as.vector = source->as.vector;
        return 1;
    case AIS_PAYLOAD_EVENT:
        if (!ais_owned_string_clone(
                &target->as.event.name, &source->as.event.name) ||
            !ais_owned_string_clone(
                &target->as.event.value, &source->as.event.value)) {
            ais_owned_payload_free(target);
            return 0;
        }
        return 1;
    case AIS_PAYLOAD_STRUCT:
        if (!ais_owned_string_clone(
                &target->as.structure.label, &source->as.structure.label) ||
            !ais_owned_string_clone(
                &target->as.structure.aux, &source->as.structure.aux)) {
            ais_owned_payload_free(target);
            return 0;
        }
        target->as.structure.x = source->as.structure.x;
        target->as.structure.y = source->as.structure.y;
        target->as.structure.width = source->as.structure.width;
        target->as.structure.height = source->as.structure.height;
        return 1;
    case AIS_PAYLOAD_BYTES:
        if (source->as.bytes.size == 0u) {
            return 1;
        }
        if (source->as.bytes.data == NULL) {
            ais_owned_payload_free(target);
            return 0;
        }
        target->as.bytes.data = malloc(source->as.bytes.size);
        if (target->as.bytes.data == NULL) {
            ais_owned_payload_free(target);
            return 0;
        }
        memcpy(target->as.bytes.data, source->as.bytes.data,
               source->as.bytes.size);
        target->as.bytes.size = source->as.bytes.size;
        return 1;
    default:
        ais_owned_payload_free(target);
        return 0;
    }
}

static void ais_owned_observation_free(ais_owned_observation_t *observation)
{
    if (observation == NULL) {
        return;
    }
    ais_owned_string_free(&observation->observation_id);
    ais_owned_string_free(&observation->session_id);
    ais_owned_string_free(&observation->observation_type);
    ais_owned_timestamp_free(&observation->acquired_at);
    ais_owned_timestamp_free(&observation->received_at);
    ais_owned_payload_free(&observation->payload);
    ais_owned_string_free(&observation->unit_id);
    ais_owned_string_free(&observation->reference_frame_id);
    ais_owned_provenance_free(&observation->provenance);
    memset(observation, 0, sizeof(*observation));
}

static int ais_owned_observation_clone(
    ais_owned_observation_t *target, const ais_owned_observation_t *source)
{
    if (target == NULL || source == NULL) {
        return 0;
    }
    ais_owned_observation_free(target);
    target->sensor_id = source->sensor_id;
    target->provider_id = source->provider_id;
    target->acquisition = source->acquisition;
    target->sequence = source->sequence;
    target->unit_code = source->unit_code;
    target->reference_frame_code = source->reference_frame_code;
    target->validity = source->validity;
    target->quality = source->quality;
    target->flags = source->flags;
    if (!ais_owned_string_clone(
            &target->observation_id, &source->observation_id) ||
        !ais_owned_string_clone(&target->session_id, &source->session_id) ||
        !ais_owned_string_clone(
            &target->observation_type, &source->observation_type) ||
        !ais_owned_string_clone(&target->unit_id, &source->unit_id) ||
        !ais_owned_string_clone(
            &target->reference_frame_id, &source->reference_frame_id) ||
        !ais_owned_timestamp_clone(
            &target->acquired_at, &source->acquired_at) ||
        !ais_owned_timestamp_clone(
            &target->received_at, &source->received_at) ||
        !ais_owned_payload_clone(&target->payload, &source->payload) ||
        !ais_owned_provenance_clone(
            &target->provenance, &source->provenance)) {
        ais_owned_observation_free(target);
        return 0;
    }
    return 1;
}
