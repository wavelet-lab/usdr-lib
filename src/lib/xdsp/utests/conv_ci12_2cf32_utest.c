// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "../conv_ci12_2cf32_2.h"

#undef DEBUG_PRINT

#define WORD_COUNT (20u)
#define IN_STREAM_SIZE_BZ (WORD_COUNT * 12u / 8u)

//#define IN_STREAM_SIZE_BZ 29u
//#define WORD_COUNT (IN_STREAM_SIZE_BZ * 8u / 12u)   // 88 i12 words

#define SPEED_WORD_COUNT (8192u)
#define SPEED_SIZE_BZ (SPEED_WORD_COUNT * 12u / 8u)

static const unsigned packet_lens[3] = { 1235, 7777, SPEED_SIZE_BZ };

#define SPEED_MEASURE_ITERS 1000000

static int16_t* in = NULL;
static float* out1 = NULL;
static float* out1_etalon = NULL;
static float* out2 = NULL;
static float* out2_etalon = NULL;
static float* out[2] = {NULL, NULL};

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    posix_memalign((void**)&in,          ALIGN_BYTES, SPEED_SIZE_BZ);
    posix_memalign((void**)&out1,        ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/2);
    posix_memalign((void**)&out1_etalon, ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/2);
    posix_memalign((void**)&out2,        ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/2);
    posix_memalign((void**)&out2_etalon, ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/2);

    out[0] = out1;
    out[1] = out2;

    //fill

    uint8_t *pin = (uint8_t*)in;

    for(int16_t i = SPEED_WORD_COUNT, j = SPEED_SIZE_BZ; i ; i -= 2, j -= 3)
    {
        int16_t v0 = i - 1;
        int16_t v1 = i - 2;

        v0 = ((i - 1) % 4) ? v0 : -v0;
        v1 = ((i - 2) % 4) ? v1 : -v1;

        pin[j-0-1] = (v0 >> 4) & 0xff;
        pin[j-1-1] = ((v0 << 4) & 0xf0) | ((v1 >> 8) & 0x0f);
        pin[j-2-1] = (v1 & 0xff);
    }
#if 0
    for(unsigned i = 0; i < WORD_COUNT; ++i)
    {
        fprintf(stderr, "%x\n", pin[i]);
    }
#endif
}

static void teardown()
{
    free(in);
    free(out1);
    free(out1_etalon);
    free(out2);
    free(out2_etalon);
}

static conv_function_t get_fn(generic_opts_t o, int log)
{
    const char* fn_name = NULL;
    conv_function_t fn = conv_get_ci12_2cf32_c(o, &fn_name);

    //ignore dups
    if(last_fn_name && !strcmp(last_fn_name, fn_name))
        return NULL;

    if(log)
        fprintf(stderr, "%-20s\t", fn_name);

    last_fn_name = fn_name;
    return fn;
}

#define CONV_SCALE (1.0f/32767)

START_TEST(conv_ci12_2cf32_check)
{
    last_fn_name = NULL;

    const void* pin = (const void*)in;
          void* pout = (void*)out;

    const size_t bzin  = IN_STREAM_SIZE_BZ;
    const size_t bzout = WORD_COUNT * sizeof(float);

    fprintf(stderr,"\n**** Check generic ***\n");

    (*get_fn(OPT_GENERIC, 1))(&pin, bzin, pout, bzout);

    fprintf(stderr, "\n");
    for(uint16_t i = 0; i < WORD_COUNT / 2; ++i)
    {
        fprintf(stderr, "%.6f ", out[0][i]);

//        float v = (float)(i << 4);
//        v *= CONV_SCALE;
//        v = (i % 4) ? v : -v;

//        fprintf(stderr, "\ni=%u\tout=%.6f\texpected=%.6f", i, out[i], v);
        //ck_assert_float_eq(v, out[i]);
    }
    fprintf(stderr, "\n");
    for(uint16_t i = 0; i < WORD_COUNT / 2; ++i)
    {
        fprintf(stderr, "%.6f ", out[1][i]);
    }

    fprintf(stderr, "\n");
}
END_TEST

START_TEST(conv_ci12_2cf32_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void** pout = (void**)out;
    last_fn_name = NULL;

    const size_t bzin  = SPEED_SIZE_BZ;
    const size_t bzout = SPEED_WORD_COUNT * sizeof(float);

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    (*get_fn(OPT_GENERIC, 0))(&pin, bzin, pout, bzout);
    memcpy(out1_etalon, out[0], bzout / 2);
    memcpy(out2_etalon, out[1], bzout / 2);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out[0], 0, bzout / 2);
            memset(out[1], 0, bzout / 2);
            (*fn)(&pin, bzin, pout, bzout);
#if 0
            fprintf(stderr, "\n\n");
            for(uint16_t i = 0; i < 16; ++i)
            {
                fprintf(stderr, "%.6f ", out[0][i]);
            }
            fprintf(stderr, "\n");
            for(uint16_t i = 0; i < 16; ++i)
            {
                fprintf(stderr, "%.6f ", out[1][i]);
            }
            fprintf(stderr, "\n\n");
#endif
            int res = memcmp(out[0], out1_etalon, bzout / 2) || memcmp(out[1], out2_etalon, bzout / 2);
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
#ifdef DEBUG_PRINT
            for(int i = 0; res && i < STREAM_SIZE_CHECK; ++i)
            {
                fprintf(stderr, "i = %d : in = %d, out = %.6f, etalon = %.6f\n", i, in[i], out[i], out_etalon[i]);
            }
#endif
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_ci12_2cf32_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void** pout = (void**)out;
    last_fn_name = NULL;

    const size_t bzin  = packet_lens[_i];
    const size_t bzout = SPEED_WORD_COUNT * sizeof(float);

    fprintf(stderr, "\n**** Compare SIMD implementations speed ***\n");
    fprintf(stderr,   "**** packet: %lu bytes, iters: %u ***\n", bzin, SPEED_MEASURE_ITERS);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            //warming
            for(int i = 0; i < 100; ++i) (*fn)(&pin, bzin, pout, bzout);

            //measuring
            uint64_t tk = clock_get_time();
            for(int i = 0; i < SPEED_MEASURE_ITERS; ++i) (*fn)(&pin, bzin, pout, bzout);
            uint64_t tk1 = clock_get_time() - tk;
            fprintf(stderr, "\t%" PRIu64 " us elapsed, %" PRIu64 " ns per 1 call, ave speed = %" PRIu64 " calls/s \n",
                    tk1, (uint64_t)(tk1*1000LL/SPEED_MEASURE_ITERS), (uint64_t)(1000000LL*SPEED_MEASURE_ITERS/tk1));
        }
    }
}
END_TEST

Suite * conv_ci12_2cf32_suite(void)
{
    Suite *s;
    TCase *tc_core;

    max_opt = cpu_vcap_get();

    s = suite_create("conv_ci12_2cf32");
    tc_core = tcase_create("XDSP");
    tcase_set_timeout(tc_core, 60);
    tcase_add_unchecked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, conv_ci12_2cf32_check);
    tcase_add_test(tc_core, conv_ci12_2cf32_check_simd);
    tcase_add_loop_test(tc_core, conv_ci12_2cf32_speed, 0, 3);

    suite_add_tcase(s, tc_core);
    return s;
}
