/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_ERRORS_H
#define AISTHRA_ERRORS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIS_OK UINT32_C(0)
#define AIS_E_INVALID_ARGUMENT UINT32_C(1)
#define AIS_E_OUT_OF_MEMORY UINT32_C(2)
#define AIS_E_UNKNOWN_SENSOR UINT32_C(3)
#define AIS_E_UNKNOWN_PROVIDER UINT32_C(4)
#define AIS_E_NO_DATA UINT32_C(5)
#define AIS_E_TYPE_MISMATCH UINT32_C(6)
#define AIS_E_STALE UINT32_C(7)
#define AIS_E_TIMESTAMP_UNCOMPARABLE UINT32_C(8)
#define AIS_E_INVALID_OBSERVATION UINT32_C(9)
#define AIS_E_DUPLICATE UINT32_C(10)
#define AIS_E_PROVIDER_UNAVAILABLE UINT32_C(11)
#define AIS_E_INVALID_STATE UINT32_C(12)
#define AIS_E_SCHEMA_MISMATCH UINT32_C(13)
#define AIS_E_UNSUPPORTED UINT32_C(14)
#define AIS_E_BUSY UINT32_C(15)
#define AIS_E_TIMEOUT UINT32_C(16)
#define AIS_E_CANCELLED UINT32_C(17)
#define AIS_E_BUFFER_TOO_SMALL UINT32_C(18)
#define AIS_E_PERMISSION UINT32_C(19)
#define AIS_E_CONFLICT UINT32_C(20)
#define AIS_E_INTERNAL UINT32_C(21)

typedef struct ais_error_info {
    ais_struct_header_t header;
    uint32_t code;
    uint8_t retryable;
    ais_string_view_t provider_key;
    ais_string_view_t sensor_key;
    ais_string_view_t operation;
    ais_string_view_t message;
} ais_error_info_t;

/* Diagnostic string views remain library-owned; callers must not free them. */
AIS_API void ais_error_info_init(ais_error_info_t *error);
AIS_API const char *ais_status_name(ais_status_t status);
AIS_API uint8_t ais_status_is_retryable(ais_status_t status);

#ifdef __cplusplus
}
#endif

#endif
