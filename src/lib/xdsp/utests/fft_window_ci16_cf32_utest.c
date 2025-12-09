#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include "xdsp_utest_common.h"
#include "fft_window_functions.h"

#define FFT_SIZE (65536)
static const unsigned packet_lens[3] = { 4096, 16384, FFT_SIZE };

#define SPEED_MEASURE_ITERS 1000000
#define EPSILON 1E-4
#define CONV_SCALE (1.0f/32767)

static wvlt_fftwi16_complex* in = NULL;
static wvlt_fftwf_complex* out = NULL;
static wvlt_fftwf_complex* out_etalon = NULL;
static float* wnd = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void recalcWnd(unsigned fft_size)
{
    for(unsigned i = 0; i < fft_size; i++)
    {
        wnd[i] = CONV_SCALE * (1 - cos(2 * M_PI * i / fft_size)) / 2;
    }
}

static void setup()
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in,          ALIGN_BYTES, sizeof(wvlt_fftwi16_complex) * FFT_SIZE);
    res = res ? res : posix_memalign((void**)&out,         ALIGN_BYTES, sizeof(wvlt_fftwf_complex) * FFT_SIZE);
    res = res ? res : posix_memalign((void**)&out_etalon,  ALIGN_BYTES, sizeof(wvlt_fftwf_complex) * FFT_SIZE);
    res = res ? res : posix_memalign((void**)&wnd,         ALIGN_BYTES, sizeof(float) * FFT_SIZE);
    ck_assert_int_eq(res, 0);

    for(unsigned i = 0; i < FFT_SIZE; ++i)
    {
        in[i][0] =  32767 * (float)(rand()) / (float)RAND_MAX;
        in[i][1] = -32768 * (float)(rand()) / (float)RAND_MAX;
    }

    recalcWnd(FFT_SIZE);
}

static void teardown(void)
{
    free(in);
    free(out);
    free(out_etalon);
    free(wnd);
}

static int32_t is_equal()
{
    for(unsigned i = 0; i < FFT_SIZE; i++)
    {
        if(fabs(out[i][0] - out_etalon[i][0]) > EPSILON) return i;
        if(fabs(out[i][1] - out_etalon[i][1]) > EPSILON) return i;
    }
    return -1;
}

START_TEST(wnd_check)
{
    generic_opts_t opt = max_opt;
    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    fft_window_ci16_cf32_c(OPT_GENERIC, NULL)(in, FFT_SIZE, wnd, out_etalon);
    last_fn_name = NULL;
    const char* fn_name = NULL;
    fft_window_ci16_cf32_function_t fn = NULL;

    while(opt != OPT_GENERIC)
    {
        fn = fft_window_ci16_cf32_c(opt, &fn_name);

        if(last_fn_name && !strcmp(last_fn_name, fn_name))
        {
            --opt;
            continue;
        }

        last_fn_name = fn_name;
        fn(in, FFT_SIZE, wnd, out);

        int res = is_equal();
        fprintf(stderr, "%-30s\t", fn_name);
        (res >= 0) ? fprintf(stderr, "\tFAILED!\n") : fprintf(stderr, "\tOK!\n");
#if 1
        if(res >= 0)
        {
            unsigned i = res;
            fprintf(stderr, "TEST  > i:%u in=(%d,%d) out=(%.6f,%.6f) <---> out_etalon=(%.6f,%.6f)\n",
                    i, in[i][0], in[i][1], out[i][0], out[i][1], out_etalon[i][0], out_etalon[i][1]);
        }
#endif
        ck_assert_int_eq( res, -1 );
        --opt;
    }
}
END_TEST

START_TEST(wnd_speed)
{
    fprintf(stderr, "\n**** Compare SIMD implementations speed ***\n");
    const char* fn_name = NULL;
    fft_window_ci16_cf32_function_t fn = NULL;

    unsigned size = packet_lens[_i];

    last_fn_name = NULL;
    generic_opts_t opt = max_opt;

    fprintf(stderr, "**** packet: %u elems, iters: %u ***\n", size, SPEED_MEASURE_ITERS);

    while(opt != OPT_GENERIC)
    {
        fn = fft_window_ci16_cf32_c(opt, &fn_name);
        if(last_fn_name && !strcmp(last_fn_name, fn_name))
        {
            --opt;
            continue;
        }
        last_fn_name = fn_name;
        fprintf(stderr, "%-30s\t", fn_name);

        //warming
        for(unsigned i = 0; i < 100; ++i) (*fn)(in, size, wnd, out);

        //measuring
        uint64_t tk = clock_get_time();
        for(unsigned i = 0; i < SPEED_MEASURE_ITERS; ++i) (*fn)(in, size, wnd, out);
        uint64_t tk1 = clock_get_time() - tk;

        fprintf(stderr, "\t%" PRIu64 " us elapsed, %" PRIu64 " ns per 1 cycle, ave speed = %" PRIu64 " cycles/s \n",
                tk1, (uint64_t)(tk1*1000LL/SPEED_MEASURE_ITERS), (uint64_t)(1000000LL*SPEED_MEASURE_ITERS/tk1));

        --opt;
    }
}
END_TEST

Suite * fft_window_ci16_cf32_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("fft_window_ci16_cf32_functions");

    ADD_REGRESS_TEST(s, wnd_check);
    ADD_PERF_LOOP_TEST(s, wnd_speed, 300, 0, 3);

    return s;
}
