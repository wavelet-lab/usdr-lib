// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_2ci16_ci16_2.h"

#undef DEBUG_PRINT

#define PACKET_SIZE (32768u)
#define OUT_BZ (PACKET_SIZE * sizeof(int16_t))

static const unsigned packet_lens[3] = { 1024u, 8192u, PACKET_SIZE };

#define SPEED_MEASURE_ITERS 1000000

static int16_t* in_0 = NULL;
static int16_t* in_1 = NULL;
static int16_t* in[2] = {NULL, NULL};

static int16_t* out = NULL;
static int16_t* out_etalon = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in_0,       ALIGN_BYTES, OUT_BZ / 2);
    res = res ? res : posix_memalign((void**)&in_1,       ALIGN_BYTES, OUT_BZ / 2);
    res = res ? res : posix_memalign((void**)&out,        ALIGN_BYTES, OUT_BZ);
    res = res ? res : posix_memalign((void**)&out_etalon, ALIGN_BYTES, OUT_BZ);
    ck_assert_int_eq(res, 0);

    in[0] = in_0;
    in[1] = in_1;

    //fill
    int16_t *p0 = in_0;
    int16_t *p1 = in_1;

    for(int i = 0; i < PACKET_SIZE; i += 4)
    {
        int sign = (float)(rand()) / (float)RAND_MAX > 0.5 ? -1 : 1;
        *p0++ = sign * (i + 0);
        *p0++ = -sign * (i + 1);
        *p1++ = -sign * (i + 2);
        *p1++ = sign * (i + 3);
    }
}

static void teardown()
{
    free(in_0);
    free(in_1);
    free(out);
    free(out_etalon);
}

static conv_function_t get_fn(generic_opts_t o, int log)
{
    return generic_get_fn(o, log, conv_get_2ci16_ci16_c, &last_fn_name);
}


START_TEST(conv_2ci16_ci16_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void** pin = (const void**)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = OUT_BZ;
    const size_t bzout = OUT_BZ;

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    (*get_fn(OPT_GENERIC, 0))(pin, bzin, &pout, bzout);
    memcpy(out_etalon, out, bzout);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out, 0, bzout);
            (*fn)(pin, bzin, &pout, bzout);
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


START_TEST(conv_2ci16_ci16_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void** pin = (const void**)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = packet_lens[_i] * sizeof(int16_t);
    const size_t bzout = bzin;

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

Suite * conv_2ci16_ci16_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("conv_2ci16_ci16");

    ADD_REGRESS_TEST(s, conv_2ci16_ci16_check_simd);
    ADD_PERF_LOOP_TEST(s, conv_2ci16_ci16_speed, 60, 0, 3);

    return s;
}
