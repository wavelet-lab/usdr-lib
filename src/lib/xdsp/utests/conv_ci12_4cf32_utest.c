// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_ci12_4cf32_2.h"

#undef DEBUG_PRINT

#define WORD_COUNT (32u)
#define IN_STREAM_SIZE_BZ (WORD_COUNT * 12u / 8u)

#define SPEED_WORD_COUNT (8192u)
#define SPEED_SIZE_BZ (SPEED_WORD_COUNT * 12u / 8u)

static const unsigned packet_lens[3] = { 1235, 7777, SPEED_SIZE_BZ };

#define SPEED_MEASURE_ITERS 1000000

static int16_t* in = NULL;
static float* out1 = NULL;
static float* out1_etalon = NULL;
static float* out2 = NULL;
static float* out2_etalon = NULL;
static float* out3 = NULL;
static float* out3_etalon = NULL;
static float* out4 = NULL;
static float* out4_etalon = NULL;

static float* out[4] = {NULL, NULL, NULL, NULL};

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in,          ALIGN_BYTES, SPEED_SIZE_BZ);
    res = res ? res : posix_memalign((void**)&out1,        ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/4);
    res = res ? res : posix_memalign((void**)&out1_etalon, ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/4);
    res = res ? res : posix_memalign((void**)&out2,        ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/4);
    res = res ? res : posix_memalign((void**)&out2_etalon, ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/4);
    res = res ? res : posix_memalign((void**)&out3,        ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/4);
    res = res ? res : posix_memalign((void**)&out3_etalon, ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/4);
    res = res ? res : posix_memalign((void**)&out4,        ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/4);
    res = res ? res : posix_memalign((void**)&out4_etalon, ALIGN_BYTES, sizeof(float) * SPEED_WORD_COUNT/4);
    ck_assert_int_eq(res, 0);

    out[0] = out1;
    out[1] = out2;
    out[2] = out3;
    out[3] = out4;

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
    free(out3);
    free(out3_etalon);
    free(out4);
    free(out4_etalon);
}

static conv_function_t get_fn(generic_opts_t o, int log)
{
    return generic_get_fn(o, log, conv_get_ci12_4cf32_c, &last_fn_name);
}

#define CONV_SCALE (1.0f/32767)

START_TEST(conv_ci12_4cf32_check)
{
    last_fn_name = NULL;

    const void* pin = (const void*)in;
    void* pout = (void*)out;

    const size_t bzin  = IN_STREAM_SIZE_BZ;
    const size_t bzout = WORD_COUNT * sizeof(float);

    fprintf(stderr,"\n**** Check generic ***\n");

    (*get_fn(OPT_GENERIC, 1))(&pin, bzin, pout, bzout);

#ifdef DEBUG_PRINT
    fprintf(stderr, "\n");
    for(unsigned j = 0; j < 4; ++j)
    {
        fprintf(stderr,"out#%d: ", j);
        for(uint16_t i = 0; i < WORD_COUNT/4; ++i)
            fprintf(stderr,"%.2f, ", out[j][i] / CONV_SCALE / 16);
        fprintf(stderr, "\n");
    }
#endif
}
END_TEST

START_TEST(conv_ci12_4cf32_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void** pout = (void**)out;
    last_fn_name = NULL;

    const size_t bzin  = SPEED_SIZE_BZ - 64 + 32 + 10;
    const size_t bzout = SPEED_WORD_COUNT * sizeof(float);

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    memset(out[0], 0, bzout / 4);
    memset(out[1], 0, bzout / 4);
    memset(out[2], 0, bzout / 4);
    memset(out[3], 0, bzout / 4);
    (*get_fn(OPT_GENERIC, 0))(&pin, bzin, pout, bzout);
    memcpy(out1_etalon, out[0], bzout / 4);
    memcpy(out2_etalon, out[1], bzout / 4);
    memcpy(out3_etalon, out[2], bzout / 4);
    memcpy(out4_etalon, out[3], bzout / 4);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out[0], 0, bzout / 4);
            memset(out[1], 0, bzout / 4);
            memset(out[2], 0, bzout / 4);
            memset(out[3], 0, bzout / 4);
            (*fn)(&pin, bzin, pout, bzout);

            int res = memcmp(out[0], out1_etalon, bzout / 4) || memcmp(out[1], out2_etalon, bzout / 4) ||
                      memcmp(out[2], out3_etalon, bzout / 4) || memcmp(out[3], out4_etalon, bzout / 4);
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
#ifdef DEBUG_PRINT
            for(unsigned j = 0; j < 4; ++j)
            {
                fprintf(stderr,"out#%d: ", j);
                for(uint16_t i = 0; i < WORD_COUNT/4; ++i)
                    fprintf(stderr,"%.2f, ", out[j][i] / CONV_SCALE / 16);
                fprintf(stderr, "\n");
            }
#endif
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_ci12_4cf32_speed)
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

Suite * conv_ci12_4cf32_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("conv_ci12_2cf32");

    ADD_REGRESS_TEST(s, conv_ci12_4cf32_check);
    ADD_REGRESS_TEST(s, conv_ci12_4cf32_check_simd);
    ADD_PERF_LOOP_TEST(s, conv_ci12_4cf32_speed, 60, 0, 3);

    return s;
}
