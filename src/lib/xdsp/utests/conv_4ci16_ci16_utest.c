// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "../conv_4ci16_ci16_2.h"

#undef DEBUG_PRINT

#define PACKET_SIZE (65536u)
#define OUT_BZ (PACKET_SIZE * sizeof(int16_t))

static const unsigned packet_lens[4] = { 8192u, 16384u, 32768u, PACKET_SIZE };

#define SPEED_MEASURE_ITERS 1000000

static int16_t* in_0 = NULL;
static int16_t* in_1 = NULL;
static int16_t* in_2 = NULL;
static int16_t* in_3 = NULL;
static int16_t* in[4] = {NULL, NULL, NULL, NULL};

static int16_t* out = NULL;
static int16_t* out_etalon = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    posix_memalign((void**)&in_0,       ALIGN_BYTES, OUT_BZ / 4);
    posix_memalign((void**)&in_1,       ALIGN_BYTES, OUT_BZ / 4);
    posix_memalign((void**)&in_2,       ALIGN_BYTES, OUT_BZ / 4);
    posix_memalign((void**)&in_3,       ALIGN_BYTES, OUT_BZ / 4);
    posix_memalign((void**)&out,        ALIGN_BYTES, OUT_BZ);
    posix_memalign((void**)&out_etalon, ALIGN_BYTES, OUT_BZ);

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

    for(int i = 0; i < PACKET_SIZE; i += 8)
    {
        int sign = (float)(rand()) / (float)RAND_MAX > 0.5 ? -1 : 1;
        *p0++ =  sign * (i + 0);
        *p0++ = -sign * (i + 1);
        *p1++ = -sign * (i + 2);
        *p1++ =  sign * (i + 3);
        *p2++ =  sign * (i + 4);
        *p2++ = -sign * (i + 5);
        *p3++ = -sign * (i + 6);
        *p3++ =  sign * (i + 7);
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
    conv_function_t fn = conv_get_4ci16_ci16_c(o, &fn_name);

    //ignore dups
    if(last_fn_name && !strcmp(last_fn_name, fn_name))
        return NULL;

    if(log)
        fprintf(stderr, "%-20s\t", fn_name);

    last_fn_name = fn_name;
    return fn;
}

static void print_data(const char* header)
{
    if(header)
        fprintf(stderr, "%s:", header);

    for(unsigned n = 0; n < 4; ++n)
    {
        fprintf(stderr, "\n in%d: ", n);
        for(unsigned i = 0; i < 8; ++i)
        {
            fprintf(stderr, "%4d ", in[n][i]);
        }
    }

    fprintf(stderr, "\n out: ");
    for(unsigned i = 0; i < 32; ++i)
    {
        fprintf(stderr, "%d ", out[i]);
    }
    fprintf(stderr, "\n");
}

START_TEST(conv_4ci16_ci16_check_simd)
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

#if 0
    print_data("ETALON DATA");
#endif

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out, 0, bzout);
            (*fn)(pin, bzin, &pout, bzout);
#if 0
            print_data(NULL);
#endif
            int res = memcmp(out, out_etalon, bzout);
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_4ci16_ci16_speed)
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

Suite * conv_4ci16_ci16_suite(void)
{
    Suite *s;
    TCase *tc_core;

    max_opt = cpu_vcap_get();

    s = suite_create("conv_4ci16_ci16");
    tc_core = tcase_create("XDSP");
    tcase_set_timeout(tc_core, 60);
    tcase_add_unchecked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, conv_4ci16_ci16_check_simd);
    tcase_add_loop_test(tc_core, conv_4ci16_ci16_speed, 0, 4);

    suite_add_tcase(s, tc_core);
    return s;
}
