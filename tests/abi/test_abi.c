/* SPDX-License-Identifier: MIT */

#include <aisthra/advanced.h>

#include <stddef.h>
#include <stdint.h>

_Static_assert(AIS_OK == 0u, "AIS_OK ABI value changed");
_Static_assert(AIS_E_INVALID_ARGUMENT == 1u, "status ABI value changed");
_Static_assert(AIS_E_INTERNAL == 21u, "status ABI value changed");
_Static_assert(sizeof(ais_status_t) == sizeof(uint32_t),
               "status must remain uint32_t");
_Static_assert(sizeof(ais_sensor_id_t) == sizeof(uint64_t),
               "sensor IDs must remain uint64_t");
_Static_assert(offsetof(ais_struct_header_t, version) == sizeof(uint32_t),
               "versioned header layout changed");

int main(void)
{
    ais_error_info_t error;
    ais_ctx_config_t ctx_config;
    ais_sensor_config_t sensor_config;
    ais_provider_config_t provider_config;
    ais_dedup_policy_t policy;
    ais_schema_config_t schema;
    uint32_t major;
    uint32_t minor;
    uint32_t patch;

    ais_error_info_init(&error);
    ais_ctx_config_init(&ctx_config);
    ais_sensor_config_init(&sensor_config);
    ais_provider_config_init(&provider_config);
    ais_dedup_policy_init(&policy);
    ais_schema_config_init(&schema);

    if (error.code != AIS_OK ||
        ctx_config.header.version != AIS_STRUCT_VERSION_1 ||
        sensor_config.header.size != sizeof(sensor_config) ||
        provider_config.header.size != sizeof(provider_config) ||
        policy.history_limit == 0u || schema.schema_major != AIS_SCHEMA_MAJOR) {
        return 1;
    }
    if (ais_library_version(&major, &minor, &patch) != AIS_OK ||
        major != 0u || minor != 1u || patch != 0u ||
        ais_api_version(&major, &minor) != AIS_OK || major != 1u ||
        ais_abi_version(&major, &minor) != AIS_OK || major != 1u ||
        ais_schema_version(&major, &minor) != AIS_OK ||
        major != AIS_SCHEMA_MAJOR || minor != AIS_SCHEMA_MINOR) {
        return 1;
    }
    if (ais_status_name(AIS_E_DUPLICATE) == NULL ||
        ais_status_is_retryable(AIS_E_DUPLICATE) != 0u ||
        ais_status_is_retryable(AIS_E_PROVIDER_UNAVAILABLE) == 0u ||
        ais_build_id() == NULL) {
        return 1;
    }
    return 0;
}
