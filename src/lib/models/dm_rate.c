// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dm_rate.h"
#include "dm_dev_impl.h"

#include "../device/device_vfs.h"

int usdr_dmr_rate_set(pdm_dev_t dev,
                      const char *name,
                      unsigned rate)
{
    return usdr_dme_set_uint(dev, name == NULL ? "/dm/rate/master" : name, rate);
}
