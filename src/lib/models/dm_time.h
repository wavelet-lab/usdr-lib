// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DM_TIME_H
#define DM_TIME_H

#include <usdr_port.h>
#include "dm_dev.h"

enum dm_time_types {
    DMT_GLOBAL = 0, ///< Device hw global timer (internal ticks)
    DMT_REALTIME = 1, ///< Realtime timer with nanoseconds precision
};

typedef uint64_t dm_time_t;

int usdr_dmt_get_id(pdm_dev_t dev, const char *timer, unsigned* time_type);

int usdr_dmt_is_supported(pdm_dev_t dev, unsigned time_type);

int usdr_dmt_reset(pdm_dev_t dev, unsigned time_type);

int usdr_dmt_get(pdm_dev_t dev, unsigned time_type, dm_time_t* ot);
int usdr_dmt_set(pdm_dev_t dev, unsigned time_type, dm_time_t it);

int usdr_dmt_sync(pdm_dev_t dev, unsigned time_type, unsigned sync_event);

/// Issue timed operation
int usdr_dmt_op(pdm_dev_t dev, unsigned time_type, dm_time_t time, ...);

#endif
