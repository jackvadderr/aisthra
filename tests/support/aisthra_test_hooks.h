/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_TEST_HOOKS_H
#define AISTHRA_TEST_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

void ais_test_alloc_reset(void);
void ais_test_alloc_fail_after(long allocations);
void ais_test_set_sensor_sequence(
    ais_ctx_t *ctx, ais_sensor_id_t sensor, uint64_t next_sequence);

#ifdef __cplusplus
}
#endif

#endif
