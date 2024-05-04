// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_FILTER_H
#define CONV_FILTER_H

#include "conv.h"

enum filter_data_alloc_flags {
    /**< Complex interleaved values */
    FDAF_INTERLEAVE = 1,

    /**< Complex separated values */
    FDAF_SEPARATED = 2,

    /**< Do interpolation, decimation otherwise */
    FDAF_INTERPOLATE = 16,

    /**< Don't use SSE/AVX variants  */
    FDAF_NO_VECTOR  = 256,
};

filter_function_t conv_filter_c(generic_opts_t cpu_cap, const char **sfunc);
filter_function_t conv_filter_interleave_c(generic_opts_t cpu_cap, const char **sfunc);
filter_function_t conv_filter_interpolate_c(generic_opts_t cpu_cap, const char **sfunc);
filter_function_t conv_filter_interpolate_interleave_c(generic_opts_t cpu_cap, const char **sfunc);

static inline filter_function_t conv_filter(unsigned flags)
{
    const generic_opts_t cap = cpu_vcap_get();

    if (flags & FDAF_INTERLEAVE) {
        return (flags & FDAF_INTERPOLATE) ?
            conv_filter_interpolate_interleave_c(cap, NULL) : conv_filter_interleave_c(cap, NULL);
    }
    else
    {
        return (flags & FDAF_INTERPOLATE) ?
            conv_filter_interpolate_c(cap, NULL) : conv_filter_c(cap, NULL);
    }
}

#endif // CONV_FILTER_H
