#include "fft_window_functions.h"
#include "attribute_switch.h"

#define TEMPLATE_FUNC_NAME fft_window_cf32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fft_window_cf32_generic.t"
DECLARE_TR_FUNC_FFT_WINDOW_CF32(fft_window_cf32_generic)

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME fft_window_cf32_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/fft_window_cf32_avx2.t"
DECLARE_TR_FUNC_FFT_WINDOW_CF32(fft_window_cf32_avx2)
#endif

#ifdef WVLT_AVX512BW
#define TEMPLATE_FUNC_NAME fft_window_cf32_avx512bw
VWLT_ATTRIBUTE(optimize("-O3"), target("avx512bw"))
#include "templates/fft_window_cf32_avx512bw.t"
DECLARE_TR_FUNC_FFT_WINDOW_CF32(fft_window_cf32_avx512bw)
#endif

#define TEMPLATE_FUNC_NAME fft_window_ci16_cf32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fft_window_ci16_cf32_generic.t"
DECLARE_TR_FUNC_FFT_WINDOW_CI16_CF32(fft_window_ci16_cf32_generic)

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME fft_window_ci16_cf32_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/fft_window_ci16_cf32_avx2.t"
DECLARE_TR_FUNC_FFT_WINDOW_CI16_CF32(fft_window_ci16_cf32_avx2)
#endif

#ifdef WVLT_AVX512BW
#define TEMPLATE_FUNC_NAME fft_window_ci16_cf32_avx512bw
VWLT_ATTRIBUTE(optimize("-O3"), target("avx512bw"))
#include "templates/fft_window_ci16_cf32_avx512bw.t"
DECLARE_TR_FUNC_FFT_WINDOW_CI16_CF32(fft_window_ci16_cf32_avx512bw)
#endif

fft_window_cf32_function_t fft_window_cf32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fft_window_cf32_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fft_window_cf32_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fft_window_cf32_avx2, cpu_cap);
    SELECT_AVX512BW_FN(fn, fname, tr_fft_window_cf32_avx512bw, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

fft_window_ci16_cf32_function_t fft_window_ci16_cf32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fft_window_ci16_cf32_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fft_window_ci16_cf32_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fft_window_ci16_cf32_avx2, cpu_cap);
    SELECT_AVX512BW_FN(fn, fname, tr_fft_window_ci16_cf32_avx512bw, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}
