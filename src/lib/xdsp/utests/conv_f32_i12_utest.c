// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "../conv_f32_i12_2.h"

#undef DEBUG_PRINT

#define PACKET_SIZE (8192u)
#define OUT_BZ (PACKET_SIZE * sizeof(float) * 3 / 8)


static const unsigned packet_lens[3] = { 1111u, 4123u, PACKET_SIZE };

#define SPEED_MEASURE_ITERS 1000000

static float* in = NULL;
static uint8_t* out = NULL;
static uint8_t* out_etalon = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    posix_memalign((void**)&in,         ALIGN_BYTES, PACKET_SIZE * sizeof(float));
    posix_memalign((void**)&out,        ALIGN_BYTES, OUT_BZ);
    posix_memalign((void**)&out_etalon, ALIGN_BYTES, OUT_BZ);

    //fill
    for(int i = 0; i < PACKET_SIZE; ++i)
    {
        in[i] = ((float)i / PACKET_SIZE) - 0.5;
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
    const char* fn_name = NULL;
    conv_function_t fn = conv_get_f32_i12_c(o, &fn_name);

    //ignore dups
    if(last_fn_name && !strcmp(last_fn_name, fn_name))
        return NULL;

    if(log)
        fprintf(stderr, "%-20s\t", fn_name);

    last_fn_name = fn_name;
    return fn;
}

START_TEST(conv_f32_i12_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = PACKET_SIZE * sizeof(float);
    const size_t bzout = OUT_BZ;

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    (*get_fn(OPT_GENERIC, 0))(&pin, bzin, &pout, bzout);
    memcpy(out_etalon, out, bzout);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out, 0, bzout);
            (*fn)(&pin, bzin, &pout, bzout);
#if 0
            fprintf(stderr, "\n");
            for(uint16_t i = 0; i < 16; ++i)
            {
                fprintf(stderr, "%.6f ", out[i]);
            }
            fprintf(stderr, "\n");
#endif
            int res = memcmp(out, out_etalon, bzout);
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


START_TEST(conv_f32_i12_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
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
    Suite *s;
    TCase *tc_core;

    max_opt = cpu_vcap_get();

    s = suite_create("conv_f32_i12");
    tc_core = tcase_create("XDSP");
    tcase_set_timeout(tc_core, 60);
    tcase_add_unchecked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, conv_f32_i12_check_simd);
    tcase_add_loop_test(tc_core, conv_f32_i12_speed, 0, 3);

    suite_add_tcase(s, tc_core);
    return s;
}
