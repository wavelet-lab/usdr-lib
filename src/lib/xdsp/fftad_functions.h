// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef FFTAD_FUNCTIONS_H
#define FFTAD_FUNCTIONS_H

#include <stdint.h>
#include "conv.h"

#ifdef __cplusplus
extern "C" {
#endif

fftad_init_function_t fftad_init_c(generic_opts_t cpu_cap, const char** sfunc);
fftad_add_function_t fftad_add_c(generic_opts_t cpu_cap, const char** sfunc);
fftad_norm_function_t fftad_norm_c(generic_opts_t cpu_cap, const char** sfunc);

fftad_init_hwi16_function_t fftad_init_hwi16_c(generic_opts_t cpu_cap, const char** sfunc);
fftad_add_hwi16_function_t fftad_add_hwi16_c(generic_opts_t cpu_cap, const char** sfunc);
fftad_norm_hwi16_function_t fftad_norm_hwi16_c(generic_opts_t cpu_cap, const char** sfunc);

static inline void fftad_init(struct fft_accumulate_data* p,  unsigned fftsz)
{
    return (*fftad_init_c(cpu_vcap_get(), NULL))(p, fftsz);
}

static inline void fftad_add(struct fft_accumulate_data* p, wvlt_fftwf_complex* d, unsigned fftsz)
{
    return (*fftad_add_c(cpu_vcap_get(), NULL))(p, d, fftsz);
}

static inline void fftad_norm(struct fft_accumulate_data* p, unsigned fftsz, float scale, float corr, float* outa)
{
    return (*fftad_norm_c(cpu_vcap_get(), NULL))(p, fftsz, scale, corr, outa);
}


static inline void fftad_init_hwi16(struct fft_accumulate_data* p,  unsigned fftsz)
{
    return (*fftad_init_hwi16_c(cpu_vcap_get(), NULL))(p, fftsz);
}

static inline void fftad_add_hwi16(struct fft_accumulate_data* p, uint16_t* d, unsigned fftsz)
{
    return (*fftad_add_hwi16_c(cpu_vcap_get(), NULL))(p, d, fftsz);
}

static inline void fftad_norm_hwi16(struct fft_accumulate_data* p, unsigned fftsz, float scale, float corr, float* outa)
{
    return (*fftad_norm_hwi16_c(cpu_vcap_get(), NULL))(p, fftsz, scale, corr, outa);
}

#ifdef __cplusplus
}
#endif

#endif // FFTAD_FUNCTIONS_H
