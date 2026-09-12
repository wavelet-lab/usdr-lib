#ifndef FFT_WINDOWS_FUNCTIONS_H
#define FFT_WINDOWS_FUNCTIONS_H

#include <stdint.h>
#include "conv.h"

#ifdef __cplusplus
extern "C" {
#endif

fft_window_cf32_function_t fft_window_cf32_c(generic_opts_t cpu_cap, const char** sfunc);
fft_window_ci16_cf32_function_t fft_window_ci16_cf32_c(generic_opts_t cpu_cap, const char** sfunc);

static inline void fft_window_cf32(wvlt_fftwf_complex* in, unsigned fftsz, float* wnd, wvlt_fftwf_complex* out)
{
    return (*fft_window_cf32_c(cpu_vcap_get(), NULL))(in, fftsz, wnd, out);
}

static inline void fft_window_ci16_cf32(wvlt_fftwi16_complex* in, unsigned fftsz, float* wnd, wvlt_fftwf_complex* out)
{
    return (*fft_window_ci16_cf32_c(cpu_vcap_get(), NULL))(in, fftsz, wnd, out);
}

#ifdef __cplusplus
}
#endif

#endif // FFT_WINDOWS_FUNCTIONS_H
