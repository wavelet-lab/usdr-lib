// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONTROLLER_LIME_H
#define CONTROLLER_LIME_H

#include "controller.h"
#include <usdr_lowlevel.h>

#if 0
int webusb_create_lime(struct webusb_ops* ctx,
                       uintptr_t param,
                       struct webusb_device** dev,
                       lldev_t lldev);
#endif

int lime_call(pdm_dev_t pmdev,
              struct sdr_call* pcall,
              unsigned outbufsz, char *outbuffer, char *inbuffer);

#endif
