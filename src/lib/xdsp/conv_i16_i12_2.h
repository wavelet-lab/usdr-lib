// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_I16_I12_H
#define CONV_I16_I12_H

#include "conv.h"

conv_function_t conv_get_i16_i12();
conv_function_t conv_get_i16_i12_c(generic_opts_t cpu_cap, const char **sfunc);

#endif
