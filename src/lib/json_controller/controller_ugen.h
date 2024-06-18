// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONTROLLER_UGEN_H
#define CONTROLLER_UGEN_H

#include "controller.h"

int general_call(pdm_dev_t dmdev, const struct sdr_call *pcall,
                 unsigned outbufsz, char *outbuffer, const char *inbuffer);

#endif
