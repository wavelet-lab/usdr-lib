// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONTROLLER_LIME_H
#define CONTROLLER_LIME_H

#include "controller.h"

int lime_call(pdm_dev_t pmdev,
              struct sdr_call* pcall,
              unsigned outbufsz, char *outbuffer, char *inbuffer);

#endif
