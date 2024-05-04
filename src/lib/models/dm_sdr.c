// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dm_sdr.h"
#include "dm_dev_impl.h"

#include "../device/device_vfs.h"

int usdr_dmsdr_set_frequency(pdm_dev_t dev,
                             const char *path,
                             usdr_frequency_t freq)
{
    return usdr_dme_set_uint(dev, path, freq);
}


int usdr_dmsdr_set_bandwidth(pdm_dev_t dev,
                             const char *path,
                             usdr_frequency_t start,
                             usdr_frequency_t stop)
{
    return usdr_dme_set_uint(dev, path, (start << 32) | stop);
}

int usdr_dmsdr_set_gain(pdm_dev_t dev,
                        const char *path,
                        unsigned gain)
{
    return usdr_dme_set_uint(dev, path, gain);
}

int usdr_dmsdr_set_path(pdm_dev_t dev,
                        const char *path,
                        unsigned p)
{
    return usdr_dme_set_uint(dev, path, p);
}

int usdr_dmsdr_set_path_str(pdm_dev_t dev,
                        const char *path,
                        const char *p)
{
    return usdr_dme_set_uint(dev, path, (uintptr_t)p);
}
