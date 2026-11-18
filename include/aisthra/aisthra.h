/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_H
#define AISTHRA_H

#include "types.h"
#include "errors.h"
#include "sensor.h"
#include "observation.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

AIS_API const char *ais_build_id(void);

#ifdef __cplusplus
}
#endif

#endif
