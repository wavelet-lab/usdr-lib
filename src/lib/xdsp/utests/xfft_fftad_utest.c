// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include "xdsp_utest_common.h"
#include "fftad_functions.h"

#undef DEBUG_PRINT

#define STREAM_SIZE 65536
static_assert( STREAM_SIZE >= 4096, "STREAM_SIZE should be >= 4096!" );
static const unsigned packet_lens[3] = { 256, 4096, STREAM_SIZE };

#define SPEED_MEASURE_ITERS 10000
#define AVGS 256

#define EPSILON 1E-4

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static wvlt_fftwf_complex* in = NULL;
static float* f_mant = NULL;
static int32_t* f_pwr = NULL;
static float* out = NULL;
static float* out_etalon = NULL;
static struct fft_accumulate_data acc;

static void setup(void)
{
    srand( time(0) );

    int res = 0;

    res = res ? res : posix_memalign((void**)&in,         ALIGN_BYTES, sizeof(wvlt_fftwf_complex) * STREAM_SIZE);
    res = res ? res : posix_memalign((void**)&f_mant,     ALIGN_BYTES, sizeof(float)         * STREAM_SIZE);
    res = res ? res : posix_memalign((void**)&f_pwr,      ALIGN_BYTES, sizeof(int32_t)       * STREAM_SIZE);
    res = res ? res : posix_memalign((void**)&out,        ALIGN_BYTES, sizeof(float)         * STREAM_SIZE);
    res = res ? res : posix_memalign((void**)&out_etalon, ALIGN_BYTES, sizeof(float)         * STREAM_SIZE);
    ck_assert_int_eq(res, 0);

    //init input data
    for(unsigned i = 0; i < STREAM_SIZE; ++i)
    {
        in[i][0] =  100.0f * (float)(rand()) / (float)RAND_MAX;
        in[i][1] = -100.0f * (float)(rand()) / (float)RAND_MAX;
    }

    //init acc
    acc.f_mant = f_mant;
    acc.f_pwr  = f_pwr;
    acc.mine   = 0.001;
}

static void teardown(void)
{
    free(in);
    free(f_mant);
    free(f_pwr);
    free(out);
    free(out_etalon);
}

static int32_t is_equal()
{
    for(unsigned i = 0; i < STREAM_SIZE; i++)
    {
        if(fabs(out[i] - out_etalon[i]) > EPSILON) return i;
    }
    return -1;
}

START_TEST(fftad_check)
{
    generic_opts_t opt = max_opt;
    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    fftad_init_c(OPT_GENERIC, NULL)(&acc, STREAM_SIZE);
    fftad_add_c(OPT_GENERIC, NULL)(&acc, in, STREAM_SIZE);
    fftad_norm_c(OPT_GENERIC, NULL)(&acc, STREAM_SIZE, 1.0, 0.0, out_etalon);

#ifdef DEBUG_PRINT
    for(unsigned i = 0; i < STREAM_SIZE; ++i)
    {
        if(i<10)
        {
            unsigned j = i ^ (STREAM_SIZE / 2);
            fprintf(stderr, "ETALON> i:%u in=(%.6f,%.6f) acc=(%.8f,%d,%.6f) out=%.6f\n",
                    i, in[j][0], in[j][1], acc.f_mant[j], acc.f_pwr[j], acc.mine, out_etalon[i]);
        }
    }
#endif

    last_fn_name = NULL;
    const char* fn_name = NULL;
    fftad_init_function_t fn_init = NULL;
    fftad_add_function_t fn_add = NULL;
    fftad_norm_function_t fn_norm = NULL;

    while(opt != OPT_GENERIC)
    {
        fn_init = fftad_init_c(opt, &fn_name);

        if(last_fn_name && !strcmp(last_fn_name, fn_name))
        {
            --opt;
            continue;
        }

        last_fn_name = fn_name;
        fn_add = fftad_add_c(opt, NULL);
        fn_norm = fftad_norm_c(opt, NULL);

        fn_init(&acc, STREAM_SIZE);
        fn_add(&acc, in, STREAM_SIZE);
        fn_norm(&acc, STREAM_SIZE, 1.0, 0.0, out);

#ifdef DEBUG_PRINT
        for(unsigned i = 0; i < STREAM_SIZE; ++i)
        {
            if(i<10)
            {
                unsigned j = i ^ (STREAM_SIZE / 2);
                fprintf(stderr, "TEST  > i:%u in=(%.6f,%.6f) acc=(%.8f,%d,%.6f) out=%.6f\n",
                        i, in[j][0], in[j][1], acc.f_mant[j], acc.f_pwr[j], acc.mine, out[i]);
            }
        }
#endif

        int res = is_equal();
        fprintf(stderr, "%-20s\t", fn_name);
        (res >= 0) ? fprintf(stderr, "\tFAILED!\n") : fprintf(stderr, "\tOK!\n");

        if(res >= 0)
        {
            unsigned i = res;
            unsigned j = i ^ (STREAM_SIZE / 2);
            fprintf(stderr, "TEST  > i:%u in=(%.6f,%.6f) acc=(%.8f,%d,%.6f) out=%.6f <---> out_etalon=%.6f\n",
                    i, in[j][0], in[j][1], acc.f_mant[j], acc.f_pwr[j], acc.mine, out[i], out_etalon[i]);
        }
        ck_assert_int_eq( res, -1 );
        --opt;
    }
}
END_TEST

START_TEST(fftad_speed)
{
    fprintf(stderr, "\n**** Compare SIMD implementations speed (%d adds + 1 norm within 1 iteration) ***\n", AVGS);

    const char* fn_name = NULL;
    fftad_init_function_t fn_init = NULL;
    fftad_add_function_t fn_add = NULL;
    fftad_norm_function_t fn_norm = NULL;

    unsigned size = packet_lens[_i];

    last_fn_name = NULL;
    generic_opts_t opt = max_opt;

    fprintf(stderr, "**** packet: %u elems, iters: %u ***\n", size, SPEED_MEASURE_ITERS);

    while(opt != OPT_GENERIC)
    {
        fn_init = fftad_init_c(opt, &fn_name);
        if(last_fn_name && !strcmp(last_fn_name, fn_name))
        {
            --opt;
            continue;
        }
        last_fn_name = fn_name;
        fn_add = fftad_add_c(opt, NULL);
        fn_norm = fftad_norm_c(opt, NULL);

        fprintf(stderr, "%-20s\t", fn_name);

        //warming
        (*fn_init)(&acc, size);
        for(unsigned i = 0; i < 100; ++i) (*fn_add)(&acc, in, size);
        (*fn_norm)(&acc, size, 1.0, 0.0, out);

        //measuring
        uint64_t tk = clock_get_time();
        (*fn_init)(&acc, size);
        for(unsigned i = 0; i < SPEED_MEASURE_ITERS; ++i)
        {
            for(unsigned j = 0; j < AVGS; ++j) (*fn_add)(&acc, in, size);
            (*fn_norm)(&acc, size, 1.0, 0.0, out);
        }
        uint64_t tk1 = clock_get_time() - tk;

        fprintf(stderr, "\t%" PRIu64 " us elapsed, %" PRIu64 " ns per 1 cycle, ave speed = %" PRIu64 " cycles/s \n",
                        tk1, (uint64_t)(tk1*1000LL/SPEED_MEASURE_ITERS), (uint64_t)(1000000LL*SPEED_MEASURE_ITERS/tk1));

        --opt;
    }
}
END_TEST

Suite * fftad_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("xfft_ftad_functions");

    ADD_REGRESS_TEST(s, fftad_check);
    ADD_PERF_LOOP_TEST(s, fftad_speed, 300, 0, 3);

    return s;
}
