// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_F32_I12_H
#define CONV_F32_I12_H

#include "conv.h"

conv_function_t conv_get_f32_i12();
conv_function_t conv_get_f32_i12_c(generic_opts_t cpu_cap, const char **sfunc);

#endif
