// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_f32_i16_2.h"

#undef DEBUG_PRINT

#define STREAM_SIZE (8192 + 16 + 8 + 7)
#define STREAM_SIZE_CHECK STREAM_SIZE
#define STREAM_SIZE_SPEED 32768

#define CONV_SCALE (1.0f/32767)
#define EPS (5E-5)

static const unsigned packet_lens[3] = { 2048, 8192, STREAM_SIZE_SPEED };

#define SPEED_MEASURE_ITERS 1000000

static float* in = NULL;
static int16_t* out = NULL;
static int16_t* out_etalon = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in,         ALIGN_BYTES, sizeof(float)   * STREAM_SIZE_SPEED);
    res = res ? res : posix_memalign((void**)&out,        ALIGN_BYTES, sizeof(int16_t) * STREAM_SIZE_SPEED);
    res = res ? res : posix_memalign((void**)&out_etalon, ALIGN_BYTES, sizeof(int16_t) * STREAM_SIZE_SPEED);
    ck_assert_int_eq(res, 0);

    srand( time(0) );

    for(unsigned i = 0; i < STREAM_SIZE_SPEED; ++i)
    {
        float sign = (float)(rand()) / (float)RAND_MAX > 0.5 ? 1.0 : -1.0;
        in[i] = sign * (float)(rand()) / (float)RAND_MAX;
    }
}

static void teardown()
{
    free(in);
    free(out);
    free(out_etalon);
}

static int is_equal()
{
    for(unsigned i = 0; i < STREAM_SIZE_CHECK; ++i)
    {
        float a = out[i];
        float b = out_etalon[i];

        a *= CONV_SCALE;
        b *= CONV_SCALE;

        float delta = fabs(a-b);
        if(delta > EPS)
        {
            fprintf(stderr, "i = %d : in = %.6f, out = %d, etalon = %d, delta = %.6f\n", i, in[i], out[i], out_etalon[i], delta);
            return 1;
        }
    }
    return 0;
}

static conv_function_t get_fn(generic_opts_t o, int log)
{
    const char* fn_name = NULL;
    conv_function_t fn = conv_get_f32_i16_c(o, &fn_name);

    //ignore dups
    if(last_fn_name && !strcmp(last_fn_name, fn_name))
        return NULL;

    if(log)
        fprintf(stderr, "%-20s\t", fn_name);

    last_fn_name = fn_name;
    return fn;
}

static void printer(const char* header)
{
    fprintf(stderr, "%s\n", header ? header : "");
    fprintf(stderr, "in:  ");
    for(unsigned i = 0; i < 16; ++i) fprintf(stderr, "%.4f ", in[i]);
    fprintf(stderr, "\nout: ");
    for(unsigned i = 0; i < 16; ++i) fprintf(stderr, "%.4f ", (float)out[i] / 32767);
    fprintf(stderr, "\n");
}

START_TEST(conv_f32_i16_check)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
          void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = STREAM_SIZE_CHECK * sizeof(float);
    const size_t bzout = STREAM_SIZE_CHECK * sizeof(int16_t);

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    (*get_fn(OPT_GENERIC, 0))(&pin, bzin, &pout, bzout);
#ifdef DEBUG_PRINT
    printer("ETALON:");
#endif
    memcpy(out_etalon, out, bzout);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out, 0, bzout);
            (*fn)(&pin, bzin, &pout, bzout);
#ifdef DEBUG_PRINT
            printer(NULL);
#endif
            int res = is_equal();
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
#ifdef DEBUG_PRINT
            for(int i = 0; res && i < STREAM_SIZE_CHECK; ++i)
            {
                fprintf(stderr, "i = %d : in = %.6f, out = %d, etalon = %d\n", i, in[i], out[i], out_etalon[i]);
            }
#endif
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST

START_TEST(conv_f32_i16_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = packet_lens[_i] * sizeof(float);
    const size_t bzout = packet_lens[_i] * sizeof(int16_t);

    fprintf(stderr, "\n**** Compare SIMD implementations speed ***\n");
    fprintf(stderr,   "**** packet: %lu bytes, iters: %u ***\n", bzin, SPEED_MEASURE_ITERS);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            //warming
            for(int i = 0; i < 100; ++i) (*fn)(&pin, bzin, &pout, bzout);

            //measuring
            uint64_t tk = clock_get_time();
            for(int i = 0; i < SPEED_MEASURE_ITERS; ++i) (*fn)(&pin, bzin, &pout, bzout);
            uint64_t tk1 = clock_get_time() - tk;
            fprintf(stderr, "\t%" PRIu64 " us elapsed, %" PRIu64 " ns per 1 call, ave speed = %" PRIu64 " calls/s \n",
                    tk1, (uint64_t)(tk1*1000LL/SPEED_MEASURE_ITERS), (uint64_t)(1000000LL*SPEED_MEASURE_ITERS/tk1));
        }
    }
}
END_TEST


Suite * conv_f32_i16_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("conv_f32_i16");

    ADD_REGRESS_TEST(s, conv_f32_i16_check);
    ADD_PERF_LOOP_TEST(s, conv_f32_i16_speed, 60, 0, 3);

    return s;
}
