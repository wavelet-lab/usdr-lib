// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_3CI16_CI16_H
#define CONV_3CI16_CI16_H

#include "conv.h"

conv_function_t conv_get_3ci16_ci16();
conv_function_t conv_get_3ci16_ci16_c(generic_opts_t cpu_cap, const char **sfunc);

#endif
