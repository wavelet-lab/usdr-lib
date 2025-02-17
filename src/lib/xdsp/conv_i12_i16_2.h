// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_I12_I16_H
#define CONV_I12_I16_H

#include "conv.h"

conv_function_t conv_get_i12_i16();
conv_function_t conv_get_i12_i16_c(generic_opts_t cpu_cap, const char **sfunc);

#endif
