// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_4CI16_CI16_2_H
#define CONV_4CI16_CI16_2_H

#include "conv.h"

conv_function_t conv_get_4ci16_ci16();
conv_function_t conv_get_4ci16_ci16_c(generic_opts_t cpu_cap, const char **sfunc);

#endif // CONV_4CI16_CI16_2_H
