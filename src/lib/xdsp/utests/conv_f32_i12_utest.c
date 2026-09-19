// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_f32_i12_2.h"

#undef DEBUG_PRINT

#define PACKET_SIZE (8192u)
#define OUT_BZ (PACKET_SIZE * sizeof(float) * 3 / 8)

#define CONV_SCALE (1.0f/32767)
#define EPS (5E-4)

static const unsigned packet_lens[3] = { 1111u, 4123u, PACKET_SIZE };

#define SPEED_MEASURE_ITERS 1000000

static float* in = NULL;
static uint8_t* out = NULL;
static uint8_t* out_etalon = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in,         ALIGN_BYTES, PACKET_SIZE * sizeof(float));
    res = res ? res : posix_memalign((void**)&out,        ALIGN_BYTES, OUT_BZ);
    res = res ? res : posix_memalign((void**)&out_etalon, ALIGN_BYTES, OUT_BZ);
    ck_assert_int_eq(res, 0);

    //fill
    srand( time(0) );

    for(int i = 0; i < PACKET_SIZE; ++i)
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

static conv_function_t get_fn(generic_opts_t o, int log)
{
    return generic_get_fn(o, log, conv_get_f32_i12_c, &last_fn_name);
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
            printf("[%u]    in:%.6f    (%.6f) -> etalon: (%.6f) delta: %.6f\n", cnt,     in[cnt],   a, c, d1);
            printf("[%u]    in:%.6f    (%.6f) -> etalon: (%.6f) delta: %.6f\n", cnt + 1, in[cnt+1], b, d, d2);
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

    fprintf(stderr, "in : ");
    for(unsigned i = 0; i < 16; ++i)
    {
        fprintf(stderr, "%.4f ", in[i]);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "out: ");
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

START_TEST(conv_f32_i12_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = PACKET_SIZE * sizeof(float) - 64 + 32 + 10;
    const size_t bzout = OUT_BZ;

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    memset(out, 0, bzout);
    (*get_fn(OPT_GENERIC, 0))(&pin, bzin, &pout, bzout);
#ifdef DEBUG_PRINT
    printer("HEADER:");
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
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_f32_i12_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = packet_lens[_i] * sizeof(float);
    const size_t bzout = bzin * 3 / 8;

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

Suite * conv_f32_i12_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("conv_f32_i12");

    ADD_REGRESS_TEST(s, conv_f32_i12_check_simd);
    ADD_PERF_LOOP_TEST(s, conv_f32_i12_speed, 60, 0, 3);

    return s;
}
