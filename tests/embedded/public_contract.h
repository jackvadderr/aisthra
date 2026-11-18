/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_EMBEDDED_PUBLIC_CONTRACT_H
#define AISTHRA_EMBEDDED_PUBLIC_CONTRACT_H

#include <aisthra/aisthra.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*aisthra_embedded_parallel_fn)(
    ais_ctx_t *ctx, ais_sensor_id_t sensor);

/* Runs the platform-independent public-contract assertions. */
int aisthra_embedded_public_contract(aisthra_embedded_parallel_fn parallel);

#ifdef __cplusplus
}
#endif

#endif
