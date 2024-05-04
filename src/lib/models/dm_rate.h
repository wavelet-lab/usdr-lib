// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DM_RATE_H
#define DM_RATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dm_dev.h"

// NULL stes the general master rate

int usdr_dmr_rate_set(pdm_dev_t dev,
                      const char* rate_name,
                      unsigned rate);

#ifdef __cplusplus
}
#endif


#endif
