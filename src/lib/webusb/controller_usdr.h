// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONTROLLER_USDR_H
#define CONTROLLER_USDR_H

#include "controller.h"
#include <usdr_lowlevel.h>

int usdr_call(pdm_dev_t dmdev, const struct sdr_call *pcall,
              unsigned outbufsz, char* outbuffer, const char *inbuffer);

#endif
