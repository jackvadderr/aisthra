# API Contracts, Errors, and Thread Safety

This document describes the public behavior summarized in the reviewed README.
The public headers remain normative for function-specific contracts.

## Runtime Concepts

### Contexts and lifetime

Create a context with `ais_ctx_create_default` or an initialized
`ais_ctx_config_t`. Close it with `ais_ctx_close(&ctx, &error)`; successful
close sets the caller's handle to `NULL`. The context must outlive its sensors,
providers, subscriptions, and snapshots.

### Sensors and observations

Sensors have a key, schema, units, reference frame, acquisition metadata, and
numeric ID. `ais_sensor_register_simple` is the shortest scalar path.
`ais_observation_view_t` is an input view; accepted observations are copied.

### Providers, percepts, and schemas

The provider API supplies lifecycle and polling callbacks through a vtable.
Application `user_data` remains application-owned; an optional destroy callback
releases it. Processing can retain derived percepts. Built-in schemas validate
payload kinds and metadata. Custom schemas may use validation and destroy
callbacks. Callback inputs are borrowed.

`ais_string_view_t` and `ais_bytes_view_t` borrow memory for the duration of a
call and do not require null termination. Accepted data is copied when retained
by the context. `ais_payload_view_t.kind` selects its active union member.

Public structures start with `ais_struct_header_t`; call the matching
initializer before use. The current public structure version is
`AIS_STRUCT_VERSION_1`. Payload kinds include bool, int, real, vector, event,
structure, and bytes. Reserved image, audio, and reference kinds are not
operational payloads in this version.

Snapshot handles are owned by the caller and must be released. Do not access a
view after its source snapshot or callback lifetime ends.

## Errors

Status constants in [`errors.h`](include/aisthra/errors.h) cover invalid
arguments, allocation failure, unknown IDs, no data, type mismatch, stale data,
incomparable timestamps, invalid observations, duplicate sequences,
unavailable providers, invalid state, schema mismatch, unsupported operations,
busy/timeout/cancelled states, insufficient buffers, permission, conflict, and
internal failure.

Initialize `ais_error_info_t` with `ais_error_info_init`. Diagnostic strings
are library-owned views and must not be freed by the caller. `retryable` is
advisory and does not require blind retries.

## Thread safety

Context-owned mutable state is internally synchronized. Normal concurrent
publication, status queries, snapshots, and lifecycle operations are covered by
the test suite, subject to the documented lifetime rules. Application-owned
callback data requires application synchronization when shared between threads.
Do not close a context from one of its callbacks.

The validation environment has a documented ThreadSanitizer runtime limitation;
normal concurrency tests are not a proof of absence of all races.