// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_I12_F32_H
#define CONV_I12_F32_H

#include "conv.h"

conv_function_t conv_get_i12_f32();
conv_function_t conv_get_i12_f32_c(generic_opts_t cpu_cap, const char **sfunc);

#endif
