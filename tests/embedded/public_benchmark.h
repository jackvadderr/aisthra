/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_EMBEDDED_PUBLIC_BENCHMARK_H
#define AISTHRA_EMBEDDED_PUBLIC_BENCHMARK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef double (*aisthra_embedded_clock_fn)(void);

/* Measures bounded public-API workloads using the platform monotonic clock. */
int aisthra_embedded_public_benchmark(aisthra_embedded_clock_fn clock_now);

#ifdef __cplusplus
}
#endif

#endif
