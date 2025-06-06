// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_4ci16_ci12_2.h"

#undef DEBUG_PRINT

#define PACKET_SIZE (8192u)
#define OUT_BZ (PACKET_SIZE * sizeof(int16_t) * 3 / 4)

static const unsigned packet_lens[3] = { 1111u, 4123u, PACKET_SIZE };

#define SPEED_MEASURE_ITERS 1000000

static int16_t* in_0 = NULL;
static int16_t* in_1 = NULL;
static int16_t* in_2 = NULL;
static int16_t* in_3 = NULL;
static int16_t* in[4] = {NULL, NULL, NULL, NULL};

static uint8_t* out = NULL;
static uint8_t* out_etalon = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in_0,       ALIGN_BYTES, PACKET_SIZE * sizeof(int16_t) / 2);
    res = res ? res : posix_memalign((void**)&in_1,       ALIGN_BYTES, PACKET_SIZE * sizeof(int16_t) / 4);
    res = res ? res : posix_memalign((void**)&in_2,       ALIGN_BYTES, PACKET_SIZE * sizeof(int16_t) / 4);
    res = res ? res : posix_memalign((void**)&in_3,       ALIGN_BYTES, PACKET_SIZE * sizeof(int16_t) / 4);
    res = res ? res : posix_memalign((void**)&out,        ALIGN_BYTES, OUT_BZ);
    res = res ? res : posix_memalign((void**)&out_etalon, ALIGN_BYTES, OUT_BZ);
    ck_assert_int_eq(res, 0);

    in[0] = in_0;
    in[1] = in_1;
    in[2] = in_2;
    in[3] = in_3;

    //fill
    int16_t *p0 = in_0;
    int16_t *p1 = in_1;
    int16_t *p2 = in_2;
    int16_t *p3 = in_3;

    srand( time(0) );

    for(int i = 0; i < PACKET_SIZE; i += 4)
    {
        *p0++ = ((int16_t)( 32767.f * (rand()) / (float)RAND_MAX) & 0xfff0);
        *p1++ = ((int16_t)(-32767.f * (rand()) / (float)RAND_MAX) & 0xfff0);
        *p2++ = ((int16_t)(-32767.f * (rand()) / (float)RAND_MAX) & 0xfff0);
        *p3++ = ((int16_t)( 32767.f * (rand()) / (float)RAND_MAX) & 0xfff0);
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
    return generic_get_fn(o, log, conv_get_4ci16_ci12_c, &last_fn_name);
}


static void printer(const char* header)
{
    fprintf(stderr, "%s\n", header ? header : "");

    for(unsigned k = 0; k < 4; ++k)
    {
        fprintf(stderr, "in[%d]: ", k);
        for(unsigned i = 0; i < 8; ++i)
        {
            fprintf(stderr, "%.d ", in[k][i] >> 4);
        }
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "out  : ");
    for(unsigned i = 0; i < 48; i += 3)
    {
        uint8_t v0 = out[i + 0];
        uint8_t v1 = out[i + 1];
        uint8_t v2 = out[i + 2];

        int16_t a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        int16_t b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        fprintf(stderr, "%d %d ", a >> 4, b >> 4);
    }
    fprintf(stderr, "\n");
}

START_TEST(conv_4ci16_ci12_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void** pin = (const void**)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = PACKET_SIZE * sizeof(int16_t);
    const size_t bzout = OUT_BZ;

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    (*get_fn(OPT_GENERIC, 0))(pin, bzin, &pout, bzout);
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
            (*fn)(pin, bzin, &pout, bzout);
#ifdef DEBUG_PRINT
            printer(NULL);
#endif
            int res = memcmp(out, out_etalon, bzout);
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_4ci16_ci12_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void** pin = (const void**)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = packet_lens[_i] * sizeof(int16_t);
    const size_t bzout = bzin * 3 / 4;

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

Suite * conv_4ci16_ci12_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("conv_4ci16_ci12");

    ADD_REGRESS_TEST(s, conv_4ci16_ci12_check_simd);
    ADD_PERF_LOOP_TEST(s, conv_4ci16_ci12_speed, 60, 0, 3);

    return s;
}
