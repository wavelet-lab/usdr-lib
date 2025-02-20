// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_4cf32_ci12_2.h"

//#define DEBUG_PRINT

#define PACKET_SIZE (8192u)
#define OUT_BZ (PACKET_SIZE * sizeof(float) * 3 / 8)

#define CONV_SCALE (1.0f/32767)
#define EPS (5E-4)

static const unsigned packet_lens[3] = { 1111u, 4123u, PACKET_SIZE };

#define SPEED_MEASURE_ITERS 1000000

static float* in_0 = NULL;
static float* in_1 = NULL;
static float* in_2 = NULL;
static float* in_3 = NULL;
static float* in[4] = {NULL, NULL, NULL, NULL};

static uint8_t* out = NULL;
static uint8_t* out_etalon = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    posix_memalign((void**)&in_0,       ALIGN_BYTES, PACKET_SIZE * sizeof(float) / 2);
    posix_memalign((void**)&in_1,       ALIGN_BYTES, PACKET_SIZE * sizeof(float) / 4);
    posix_memalign((void**)&in_2,       ALIGN_BYTES, PACKET_SIZE * sizeof(float) / 4);
    posix_memalign((void**)&in_3,       ALIGN_BYTES, PACKET_SIZE * sizeof(float) / 4);
    posix_memalign((void**)&out,        ALIGN_BYTES, OUT_BZ);
    posix_memalign((void**)&out_etalon, ALIGN_BYTES, OUT_BZ);

    in[0] = in_0;
    in[1] = in_1;
    in[2] = in_2;
    in[3] = in_3;

    //fill
    float *p0 = in_0;
    float *p1 = in_1;
    float *p2 = in_2;
    float *p3 = in_3;

    srand( time(0) );

    for(int i = 0; i < PACKET_SIZE; i += 4)
    {
        *p0++ =   (float)(rand()) / (float)RAND_MAX;
        *p1++ = - (float)(rand()) / (float)RAND_MAX;
        *p2++ = - (float)(rand()) / (float)RAND_MAX;
        *p3++ =   (float)(rand()) / (float)RAND_MAX;
    }
}

static void teardown()
{
    free(in_0);
    free(in_1);
    free(in_2);
    free(in_3);
    free(out);
    free(out_etalon);
}

static conv_function_t get_fn(generic_opts_t o, int log)
{
    const char* fn_name = NULL;
    conv_function_t fn = conv_get_4cf32_ci12_c(o, &fn_name);

    //ignore dups
    if(last_fn_name && !strcmp(last_fn_name, fn_name))
        return NULL;

    if(log)
        fprintf(stderr, "%-20s\t", fn_name);

    last_fn_name = fn_name;
    return fn;
}

static int is_equal()
{
    int res = 0;
    int i = OUT_BZ;
    const uint8_t* buf_got = out;
    const uint8_t* buf_eta = out_etalon;

    unsigned cnt = 0;

    while(i >= 3)
    {
        uint8_t v0 = *(buf_got++);
        uint8_t v1 = *(buf_got++);
        uint8_t v2 = *(buf_got++);

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        uint8_t e0 = *(buf_eta++);
        uint8_t e1 = *(buf_eta++);
        uint8_t e2 = *(buf_eta++);

        float c = (int16_t) (((uint16_t)e0 << 4) | ((uint16_t)e1 << 12));
        float d = (int16_t) (((uint16_t)e2 << 8) | (e1 & 0xf0));

        a *= CONV_SCALE;
        b *= CONV_SCALE;
        c *= CONV_SCALE;
        d *= CONV_SCALE;

        float d1 = fabs(a - c);
        float d2 = fabs(b - d);

        if(d1 > EPS || d2 > EPS)
        {
            printf("[%u] (%.6f) -> etalon: (%.6f) delta: %.6f\n", cnt,     a, c, d1);
            printf("[%u] (%.6f) -> etalon: (%.6f) delta: %.6f\n", cnt + 1, b, d, d2);
            return 1;
        }

        cnt += 2;
        i -= 3;
    }

    return res;
}

static void printer(const char* header)
{
    fprintf(stderr, "%s\n", header ? header : "");

    for(unsigned k = 0; k < 4; ++k)
    {
        fprintf(stderr, "in[%d]: ", k);
        for(unsigned i = 0; i < 4; ++i)
        {
            fprintf(stderr, "%.4f ", in[k][i]);
        }
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "out  : ");
    for(unsigned i = 0; i < 24; i += 3)
    {
        uint8_t v0 = out[i + 0];
        uint8_t v1 = out[i + 1];
        uint8_t v2 = out[i + 2];

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        a *= CONV_SCALE;
        b *= CONV_SCALE;

        fprintf(stderr, "%.4f %.4f ", a, b);
    }
    fprintf(stderr, "\n");
}

START_TEST(conv_4cf32_ci12_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void** pin = (const void**)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = PACKET_SIZE * sizeof(float);
    const size_t bzout = OUT_BZ;

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    (*get_fn(OPT_GENERIC, 0))(pin, bzin, &pout, bzout);
    printer("ETALON:");
    memcpy(out_etalon, out, bzout);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out, 0, bzout);
            (*fn)(pin, bzin, &pout, bzout);
#ifdef DEBUG_PRINT
            printer(NULL);
#endif \
    //int res = memcmp(out, out_etalon, bzout);
            int res = is_equal();
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_4cf32_ci12_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void** pin = (const void**)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = packet_lens[_i] * sizeof(float);
    const size_t bzout = OUT_BZ;

    fprintf(stderr, "\n**** Compare SIMD implementations speed ***\n");
    fprintf(stderr,   "**** packet: %lu bytes, iters: %u ***\n", bzin, SPEED_MEASURE_ITERS);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            //warming
            for(int i = 0; i < 100; ++i) (*fn)(pin, bzin, &pout, bzout);

            //measuring
            uint64_t tk = clock_get_time();
            for(int i = 0; i < SPEED_MEASURE_ITERS; ++i) (*fn)(pin, bzin, &pout, bzout);
            uint64_t tk1 = clock_get_time() - tk;
            fprintf(stderr, "\t%" PRIu64 " us elapsed, %" PRIu64 " ns per 1 call, ave speed = %" PRIu64 " calls/s \n",
                    tk1, (uint64_t)(tk1*1000LL/SPEED_MEASURE_ITERS), (uint64_t)(1000000LL*SPEED_MEASURE_ITERS/tk1));
        }
    }
}
END_TEST

Suite * conv_4cf32_ci12_suite(void)
{
    Suite *s;
    TCase *tc_core;

    max_opt = cpu_vcap_get();

    s = suite_create("conv_4cf32_ci12");
    tc_core = tcase_create("XDSP");
    tcase_set_timeout(tc_core, 60);
    tcase_add_unchecked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, conv_4cf32_ci12_check_simd);
    tcase_add_loop_test(tc_core, conv_4cf32_ci12_speed, 0, 3);

    suite_add_tcase(s, tc_core);
    return s;
}
