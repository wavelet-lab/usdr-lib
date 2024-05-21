#include "fft_window_functions.h"
#include "attribute_switch.h"

#define TEMPLATE_FUNC_NAME fft_window_cf32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fft_window_cf32_generic.t"
DECLARE_TR_FUNC_FFT_WINDOW_CF32(fft_window_cf32_generic)

#ifdef __AVX2__
#define TEMPLATE_FUNC_NAME fft_window_cf32_avx2
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fft_window_cf32_avx2.t"
DECLARE_TR_FUNC_FFT_WINDOW_CF32(fft_window_cf32_avx2)
#endif

fft_window_cf32_function_t fft_window_cf32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fft_window_cf32_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fft_window_cf32_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fft_window_cf32_avx2, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}
