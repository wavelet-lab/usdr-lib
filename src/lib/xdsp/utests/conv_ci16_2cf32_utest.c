// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_ci16_2cf32_2.h"

#undef DEBUG_PRINT

#define WORD_COUNT (4096u + 77u)
#define IN_STREAM_SIZE_BZ (WORD_COUNT * sizeof(int16_t))

#define SPEED_WORD_COUNT (32768u)
#define SPEED_SIZE_BZ (SPEED_WORD_COUNT * sizeof(int16_t))

static const unsigned packet_lens[3] = { 1024, 16384, SPEED_SIZE_BZ };

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
    int res = 0;
    res = res ? res : posix_memalign((void**)&in,          ALIGN_BYTES, SPEED_SIZE_BZ);
    res = res ? res : posix_memalign((void**)&out1,        ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/2);
    res = res ? res : posix_memalign((void**)&out1_etalon, ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/2);
    res = res ? res : posix_memalign((void**)&out2,        ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/2);
    res = res ? res : posix_memalign((void**)&out2_etalon, ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/2);
    ck_assert_int_eq(res, 0);

    out[0] = out1;
    out[1] = out2;

    //fill
    for(unsigned i = 0; i < SPEED_WORD_COUNT; ++i)
    {
        int sign = (float)(rand()) / (float)RAND_MAX > 0.5 ? -1 : 1;
        in[i] = sign * 32767 * (float)(rand()) / (float)RAND_MAX;
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
    return generic_get_fn(o, log, conv_get_ci16_2cf32_c, &last_fn_name);
}

#define CONV_SCALE (1.0f/32767)

static void printer(const char* header)
{
    fprintf(stderr, "%s\n", header ? header : "");
    fprintf(stderr, "in:     ");
    for(unsigned i = 0; i < 16; ++i) fprintf(stderr, "%d ", in[i]);

    for(unsigned k = 0; k < 2; ++k)
    {
        fprintf(stderr, "\nout[%d]: ", k);
        for(unsigned i = 0; i < 8; ++i) fprintf(stderr, "%d ", (int16_t)(32767 * out[k][i]));
    }
    fprintf(stderr, "\n");
}

START_TEST(conv_ci16_2cf32_check_simd)
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
#ifdef DEBUG_PRINT
    printer("ETALON:");
#endif
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
#ifdef DEBUG_PRINT
            printer(NULL);
#endif
            int res = memcmp(out[0], out1_etalon, bzout / 2) || memcmp(out[1], out2_etalon, bzout / 2);
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_ci16_2cf32_speed)
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

Suite * conv_ci16_2cf32_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("conv_ci16_2cf32");

    ADD_REGRESS_TEST(s, conv_ci16_2cf32_check_simd);
    ADD_PERF_LOOP_TEST(s, conv_ci16_2cf32_speed, 60, 0, 3);

    return s;
}
