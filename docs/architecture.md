# Architecture

Aisthra uses a single C17 engine. Its main runtime object, `ais_ctx_t`, manages
sensors, providers, schemas, retained observations, percepts, subscriptions,
and internal synchronization.

For a visual overview with multiple sensor sources, see the
[README architecture diagram](../README.md#architecture).

## Main Concepts

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

## Processing Flow

The public observation workflow is:

```text
Application / provider
    -> observation view
    -> basic validation
    -> schema validation
    -> sequence checks
    -> retained copy (if accepted and retained)
    -> optional percept processing
    -> operation result and applicable callbacks
```

The precise behavior of validation, ordering, deduplication, and processing
is controlled by the applicable public API contracts and configured policies.

## Memory Ownership

Input strings and byte buffers are borrowed for the duration of a call.
Accepted and retained observations are copied into context-managed storage.
Snapshot handles must be released by the caller using their matching release APIs.
Views must not be accessed after their owning snapshot or callback lifetime ends.

For error codes and synchronization constraints, see [Contracts](contracts.md).
