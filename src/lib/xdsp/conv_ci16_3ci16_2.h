// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_CI16_3CI16_H
#define CONV_CI16_3CI16_H

#include "conv.h"

conv_function_t conv_get_ci16_3ci16();
conv_function_t conv_get_ci16_3ci16_c(generic_opts_t cpu_cap, const char **sfunc);

#endif //CONV_CI16_3CI16_H
