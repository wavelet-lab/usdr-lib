// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "controller_lime.h"

int lime_call(pdm_dev_t pdev,
              struct sdr_call* pcall,
              unsigned outbufsz, char *outbuffer, char *inbuffer)
{
    switch (pcall->call_type) {
    case SDR_FLASH_READ:
    case SDR_FLASH_WRITE_SECTOR:
    case SDR_FLASH_ERASE:
    case SDR_CALIBRATE:
        return -ENOTSUP;
    default:
        break;
    }
    return -EINVAL;
}
