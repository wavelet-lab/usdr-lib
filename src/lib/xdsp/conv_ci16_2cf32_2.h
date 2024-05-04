// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_CI16_2CF32_H
#define CONV_CI16_2CF32_H

#include "conv.h"

conv_function_t conv_get_ci16_2cf32();
conv_function_t conv_get_ci16_2cf32_c(generic_opts_t cpu_cap, const char **sfunc);

#endif
