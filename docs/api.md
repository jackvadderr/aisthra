# Public API Reference

**Aisthra 0.1.0 (candidate)** · C17 · C++17-compatible public headers

This guide explains what each public Aisthra function is for, how the main parts
of the API fit together, and how to use them safely. It covers the 77 functions
listed by the project. It is intended as a practical companion to the public
headers, not a replacement for their exact declarations.

> **Source of truth:** `include/aisthra/*.h` defines the exact function
> signatures, argument types, structure layouts, constants, and return-value
> contracts. The original headers were not available for verification when
> this guide was prepared. Accordingly, this document does not invent
> unverified prototypes, parameter names, or function-specific error codes.

## Contents

- [Getting started](#getting-started)
- [API conventions](#api-conventions)
- [Contexts and sensors](#contexts-and-sensors)
- [Observations and publication](#observations-and-publication)
- [Providers](#providers)
- [State, snapshots, and percepts](#state-snapshots-and-percepts)
- [Schemas, processing, and events](#schemas-processing-and-events)
- [Payloads and supporting types](#payloads-and-supporting-types)
- [Errors, capabilities, and versions](#errors-capabilities-and-versions)
- [Lifecycle and ownership checklist](#lifecycle-and-ownership-checklist)
- [Header index](#header-index)

## Getting started

The simplest path does not require implementing a provider:

1. Create a context.
2. Register a sensor with a schema and unit.
3. Publish a reading.
4. Retrieve its latest value.
5. Close the context.

```c
#include <aisthra/aisthra.h>
#include <stdio.h>

int main(void)
{
    ais_ctx_t *ctx = NULL;
    ais_sensor_id_t sensor = 0u;
    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_status_t status;
    double latest = 0.0;

    ais_error_info_init(&error);
    ais_ingest_result_init(&result);

    status = ais_ctx_create_default(&ctx, &error);
    if (status == AIS_OK)
        status = ais_sensor_register_simple(
            ctx, AIS_SV("temperature-0"),
            AIS_SV("environment.temperature.v1"),
            AIS_SV("celsius"), &sensor, &error);

    if (status == AIS_OK)
        status = ais_publish_f64(ctx, sensor, 23.5, &result, &error);
    if (status == AIS_OK)
        status = ais_latest_f64(ctx, sensor, &latest, &error);

    if (status == AIS_OK)
        printf("Temperature: %.1f C\n", latest);
    else
        fprintf(stderr, "Aisthra: %s\n", ais_status_name(status));

    if (ctx != NULL)
        (void)ais_ctx_close(&ctx, &error);
    return status == AIS_OK ? 0 : 1;
}
```

The complete maintained example is
[`examples/c/basic_temperature.c`](../examples/c/basic_temperature.c).
For a physical ESP32 sensor, see the Arduino example in
[`examples/arduino/`](../examples/arduino/).

## API conventions

### Contexts, IDs, and handles

- `ais_ctx_t` is the central context. It owns internal runtime state.
- Sensor and provider IDs identify objects registered with a context.
- Snapshot handles represent copied information and must be released using
  their corresponding Aisthra release functions.
- An application must not use a handle after its lifetime ends.

### Initialization and results

Initialize public configuration, result, status, and view structures with their
matching `*_init` functions before use. Public structures use versioned headers
so the library can validate their expected layout.

Functions that report an `ais_status_t` should be checked against `AIS_OK`.
`ais_error_info_t` provides additional diagnostics where supported; do not
assume that every function accepts an error-output argument or returns a
status code. Consult the declaration for each function.

### Strings, buffers, and ownership

`ais_string_view_t` and `ais_bytes_view_t` describe borrowed data. Their
contents need not be null-terminated. Unless the particular function says
otherwise, input data remains owned by the caller during the call. Accepted
observations are copied when retained by the context.

Views exposed by snapshots or callbacks must not outlive the object or callback
that makes them valid. See [Contracts](contracts.md) for detailed ownership,
lifetime, error, and thread-safety rules.

## Contexts and sensors

**Header:** [`sensor.h`](../include/aisthra/sensor.h)

A context contains the runtime state for one Aisthra instance. Sensors
identify data streams within it. Register sensors before publishing their
observations.

| Function | Purpose |
|---|---|
| `ais_ctx_config_init` | Initialize the configuration structure used for custom context creation. |
| `ais_ctx_create` | Create a context from an application-supplied configuration. |
| `ais_ctx_create_default` | Create a context using default settings; the shortest entry point. |
| `ais_ctx_close` | Close a context and release its managed resources. A successful close clears the caller's context handle. |
| `ais_sensor_config_init` | Initialize a sensor configuration structure. |
| `ais_sensor_register` | Register a sensor using the detailed configuration API. |
| `ais_sensor_register_simple` | Register a sensor using its key, schema, and unit without building a full sensor configuration. |
| `ais_sensor_unregister` | Unregister an existing sensor. |
| `ais_sensor_status_init` | Initialize a sensor-status output structure. |
| `ais_sensor_get_status` | Query status information about a registered sensor. |

### When to use each creation function

Use `ais_ctx_create_default` when the default resource limits and processing
settings meet the application's needs. Use `ais_ctx_config_init` followed by
`ais_ctx_create` when a configuration must be supplied explicitly.

Use `ais_sensor_register_simple` for simple readings such as temperature or
humidity. Use `ais_sensor_config_init` and `ais_sensor_register` when metadata
such as a provider, reference frame, or acquisition configuration is needed.

A single context can register several sensors, each identified by its own
`ais_sensor_id_t`. Their schema and unit must match the observations published
to each sensor.

## Observations and publication

**Header:** [`observation.h`](../include/aisthra/observation.h)

An **observation** is an individual reading or event associated with a sensor.
The convenient typed functions cover common scalar and byte-oriented data;
structured observations use the general publication API.

| Function | Purpose |
|---|---|
| `ais_observation_view_init` | Initialize an input observation view. |
| `ais_ingest_result_init` | Initialize the output structure for a publication result. |
| `ais_observation_validate` | Validate an observation against the applicable public and context rules without treating validation itself as publication. |
| `ais_publish_observation` | Publish an observation described by a general observation view. |
| `ais_publish_f64` | Publish a floating-point (`double`) reading to a sensor. |
| `ais_publish_i64` | Publish a signed 64-bit integer reading. |
| `ais_publish_bool` | Publish a boolean reading. |
| `ais_publish_bytes` | Publish a byte-buffer reading. |
| `ais_latest_f64` | Retrieve the latest floating-point value for a sensor. |
| `ais_latest_i64` | Retrieve the latest signed 64-bit value. |
| `ais_latest_bool` | Retrieve the latest boolean value. |
| `ais_latest_bytes` | Retrieve the latest byte-oriented value; consult the header for buffer size and output ownership. |
| `ais_sensor_latest_observation` | Obtain a snapshot of a sensor's latest retained observation. |
| `ais_observation_snapshot_get_view` | Inspect the observation contained in a snapshot through a borrowed view. |
| `ais_observation_snapshot_release` | Release an observation snapshot. |

### Typed values or general observations?

For a simple scalar, prefer the typed `ais_publish_*` / `ais_latest_*` pair.
For metadata-rich observations or payload kinds that do not have a dedicated
typed convenience function, initialize an `ais_observation_view_t`, populate
its documented fields, then call `ais_publish_observation`.

A publication can fail or be classified according to validation, schema,
sequence, and availability rules. Do not interpret a successful API call as
proof that every possible reading was retained or that a percept was generated;
inspect the documented ingestion result where relevant.

### Observation snapshots

A typed `ais_latest_*` call is convenient when only the value is required. A
snapshot is useful when the application needs the observation's metadata in
addition to its payload. Release the snapshot using
`ais_observation_snapshot_release` after accessing it. Any borrowed view
obtained from that snapshot becomes invalid after release.

## Providers

**Header:** [`provider.h`](../include/aisthra/provider.h)

A **provider** supplies observations and can expose an acquisition lifecycle.
Providers are useful for device drivers, external data sources, and polling
components. They are optional for basic direct publication.

| Function | Purpose |
|---|---|
| `ais_provider_config_init` | Initialize provider registration configuration, including callbacks. |
| `ais_provider_register` | Register a provider with a context. |
| `ais_provider_start` | Request that a registered provider start its acquisition lifecycle. |
| `ais_provider_poll` | Invoke the provider's polling path where supported. |
| `ais_provider_stop` | Request that a provider stop its acquisition lifecycle. |
| `ais_provider_set_availability` | Update the availability information associated with a provider. |
| `ais_provider_status_init` | Initialize a provider-status output structure. |
| `ais_provider_get_status` | Query a provider's status and reported activity. |
| `ais_provider_unregister` | Remove a provider from the context. |

A provider configuration includes callback hooks and application-owned
`user_data`. When a destroy callback is configured, observe the exact cleanup
contract in the public header. The context does not automatically own every
external resource referenced by `user_data`.

**Typical lifecycle:** register → start → poll or emit → stop → unregister.
Not every provider requires polling, and calls must still follow the state
rules of the relevant implementation.

## State, snapshots, and percepts

**Header:** [`state.h`](../include/aisthra/state.h)

The state API provides summaries and snapshots of retained information.
**Percepts** are results derived from processing observations where the
selected processing path supports them; not every observation produces one.

| Function | Purpose |
|---|---|
| `ais_state_summary_init` | Initialize a state-summary output structure. |
| `ais_ctx_get_state_summary` | Read a summary of the context's state. |
| `ais_ctx_refresh` | Request a state refresh using the documented context behavior. |
| `ais_ctx_state_snapshot` | Create a snapshot of the context's retained state. |
| `ais_state_snapshot_release` | Release a state snapshot. |
| `ais_state_snapshot_observation_count` | Retrieve the number of observation entries in a state snapshot. |
| `ais_state_snapshot_observation_at` | Obtain an observation snapshot at a specified index. |
| `ais_state_snapshot_percept_count` | Retrieve the number of percept entries in a state snapshot. |
| `ais_state_snapshot_percept_at` | Obtain a percept snapshot at a specified index. |
| `ais_percept_view_init` | Initialize a percept-view structure. |
| `ais_percept_snapshot_get_view` | Read a borrowed view of a percept snapshot. |
| `ais_percept_snapshot_release` | Release a percept snapshot. |

### Snapshot ownership

A state snapshot is a separately managed handle. Observation or percept
snapshots returned by indexed access also have release functions; follow the
specific ownership rules in the headers. Never retain a view beyond the
lifetime of the object from which it was obtained.

A snapshot is a view of information retained at the time it is produced; it
should not be confused with a live subscription to future changes.

## Schemas, processing, and events

**Header:** [`advanced.h`](../include/aisthra/advanced.h)

Schemas describe acceptable observation content. Sequence policies control
how repeated or out-of-order readings are evaluated. Events allow an
application to react to selected activity.

| Function | Purpose |
|---|---|
| `ais_schema_config_init` | Initialize configuration for a custom schema. |
| `ais_schema_register` | Register an application-defined schema and its validation behavior. |
| `ais_schema_unregister` | Unregister an application-defined schema. |
| `ais_dedup_policy_init` | Initialize a sequence-deduplication policy. |
| `ais_ctx_set_dedup_policy` | Configure the context's sequence-deduplication behavior. |
| `ais_process` | Request explicit processing when using a manual-processing workflow. |
| `ais_event_subscribe` | Subscribe to selected events with an application callback. |
| `ais_event_unsubscribe` | Remove an existing event subscription. |

### When advanced functionality is useful

Use custom schemas when built-in schemas do not adequately describe the data.
Use a deduplication policy when sequence handling needs explicit control. Use
manual processing only when it fits the application's chosen processing mode.
Use event subscriptions to respond to supported events rather than polling
for every change.

Callbacks receive borrowed inputs. Protect shared application data if a
callback can run concurrently with other application code. Do not close the
context from one of its callbacks.

## Payloads and supporting types

**Header:** [`types.h`](../include/aisthra/types.h)

These functions initialize or construct values used by the observation API.
Prefer them over manually assembling structures when a matching helper exists.

| Function | Purpose |
|---|---|
| `ais_timestamp_init` | Initialize a timestamp structure. |
| `ais_timestamp_set` | Set timestamp fields through the public helper. |
| `ais_quality_init` | Initialize observation-quality metadata. |
| `ais_provenance_init` | Initialize observation-provenance metadata. |
| `ais_payload_init` | Initialize a payload view or descriptor. |
| `ais_string_view_make` | Build a string view from explicitly supplied data and length. |
| `ais_string_view_from_cstr` | Build a string view from a null-terminated C string. |
| `ais_payload_set_bool` | Select and set a boolean payload. |
| `ais_payload_set_int` | Select and set an integer payload. |
| `ais_payload_set_real` | Select and set a real-number payload. |
| `ais_payload_set_vector` | Select and set a vector payload. |
| `ais_payload_set_event` | Select and set an event payload. |
| `ais_payload_set_structure` | Select and set a structured payload. |
| `ais_payload_set_bytes` | Select and set a byte-buffer payload. |

The payload `kind` determines which value representation is active. Do not
interpret a payload as a different kind without checking it first. Built-in
payload kinds include boolean, integer, real, vector, event, structure, and
bytes. Reserved kinds are not necessarily usable in this release.

**Example of a string view:** `AIS_SV("temperature-0")` is used by the simple
registration functions. Such views do not transfer ownership of the input
string.

## Errors, capabilities, and versions

**Headers:** [`errors.h`](../include/aisthra/errors.h),
[`sensor.h`](../include/aisthra/sensor.h),
[`aisthra.h`](../include/aisthra/aisthra.h)

| Function | Purpose |
|---|---|
| `ais_error_info_init` | Initialize the diagnostic structure used by error-reporting APIs. |
| `ais_status_name` | Convert a status value to its readable name. |
| `ais_status_is_retryable` | Ask whether a status is classified as potentially retryable. |
| `ais_feature_supported` | Query support for a defined feature identifier. |
| `ais_library_version` | Query the library's version information. |
| `ais_api_version` | Query the public API version. |
| `ais_abi_version` | Query the public ABI version. |
| `ais_schema_version` | Query the schema version. |
| `ais_build_id` | Obtain the library's build identifier. |

### Error-handling pattern

```c
if (status != AIS_OK) {
    fprintf(stderr, "Aisthra operation failed: %s\n",
            ais_status_name(status));
}
```

For additional information, inspect an initialized `ais_error_info_t` where
the called function supports it. Retryability is advisory; do not retry
unconditionally or without considering side effects.

### Version numbers

For the planned `0.1.0` candidate, the reviewed project documentation
reports API `1.0`, ABI `1.0`, schema `1.0`, and build ID
`aisthra-c17-0.1.0`. Verify these values against the actual compiled library
when preparing a release.

## Lifecycle and ownership checklist

1. **Create:** initialize any configuration, then create a context.
2. **Register:** register sensors and optional providers before using them.
3. **Publish:** initialize results, publish observations, and inspect status.
4. **Read:** use typed latest-value APIs or acquire a snapshot when metadata
   is needed.
5. **Release snapshots:** match each acquired snapshot with its release API.
6. **Unregister and close:** honor active callbacks, application-owned state,
   and documented lifetime rules. A successful `ais_ctx_close` clears its
   caller-owned handle.

Refer to [Contracts](contracts.md) for detailed lifetime, memory, concurrency,
and error semantics.

## Header index

| Header | API area |
|---|---|
| [`aisthra.h`](../include/aisthra/aisthra.h) | Primary include for normal application use. |
| [`types.h`](../include/aisthra/types.h) | Core types, views, timestamps, metadata, payload helpers. |
| [`errors.h`](../include/aisthra/errors.h) | Status codes and diagnostic helpers. |
| [`sensor.h`](../include/aisthra/sensor.h) | Context, sensors, features, and version queries. |
| [`observation.h`](../include/aisthra/observation.h) | Observation publication, reading, validation, snapshots. |
| [`provider.h`](../include/aisthra/provider.h) | Acquisition providers and lifecycle. |
| [`state.h`](../include/aisthra/state.h) | State summaries, observation/percept snapshots. |
| [`advanced.h`](../include/aisthra/advanced.h) | Custom schemas, sequencing policy, processing, subscriptions. |

This guide is organized by responsibility, not by the order in which symbols
appear in the header files. Exact signatures and per-function error conditions
must always be checked in the public headers before release.
