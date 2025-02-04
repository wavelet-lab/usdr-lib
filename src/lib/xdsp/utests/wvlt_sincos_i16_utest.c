// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "../nco.h"
#include <math.h>

#define DEBUG_PRINT

#define WORD_COUNT (65536)
#define STREAM_SIZE_BZ (WORD_COUNT * sizeof(int16_t))

#define SPEED_WORD_COUNT (1000000)
#define SPEED_SIZE_BZ (SPEED_WORD_COUNT * sizeof(int16_t))

#define CONV_SCALE (32767)
#define EPSILON 5

static const unsigned packet_lens[3] = { 100000, 500000, SPEED_WORD_COUNT };

static int16_t* in_check = NULL;
static int16_t* in = NULL;
static int16_t* sindata = NULL;
static int16_t* sindata_etalon = NULL;
static int16_t* cosdata = NULL;
static int16_t* cosdata_etalon = NULL;
static int16_t* out[2] = {NULL, NULL};

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    posix_memalign((void**)&in_check,       ALIGN_BYTES, STREAM_SIZE_BZ);
    posix_memalign((void**)&in,             ALIGN_BYTES, SPEED_SIZE_BZ);
    posix_memalign((void**)&sindata,        ALIGN_BYTES, SPEED_SIZE_BZ);
    posix_memalign((void**)&sindata_etalon, ALIGN_BYTES, STREAM_SIZE_BZ);
    posix_memalign((void**)&cosdata,        ALIGN_BYTES, SPEED_SIZE_BZ);
    posix_memalign((void**)&cosdata_etalon, ALIGN_BYTES, STREAM_SIZE_BZ);

    srand( time(0) );

    //fill for check
    int16_t phase = - (CONV_SCALE + 1);
    for(unsigned i = 0; i < WORD_COUNT; ++i )
    {
        in_check[i] = phase++;
    }

    //fill for speed
    for(unsigned i = 0; i < SPEED_WORD_COUNT; ++i)
    {
        int sign = (float)(rand()) / (float)RAND_MAX > 0.5 ? -1 : 1;
        in[i] = sign * (sign == -1 ? CONV_SCALE + 1 : CONV_SCALE) * (float)(rand()) / (float)RAND_MAX;
    }
}

static void teardown()
{
    free(in_check);
    free(in);
    free(sindata);
    free(sindata_etalon);
    free(cosdata);
    free(cosdata_etalon);
}

static conv_function_t get_fn(generic_opts_t o, int log)
{
    const char* fn_name = NULL;
    conv_function_t fn = get_wvlt_sincos_i16_c(o, &fn_name);

    //ignore dups
    if(last_fn_name && !strcmp(last_fn_name, fn_name))
        return NULL;

    if(log)
        fprintf(stderr, "%-20s\t", fn_name);

    last_fn_name = fn_name;
    return fn;
}

static int32_t is_equal()
{
    for(unsigned i = 0; i < WORD_COUNT; i++)
    {
        if(abs(sindata[i] - sindata_etalon[i]) > EPSILON) return i;
        if(abs(cosdata[i] - cosdata_etalon[i]) > EPSILON) return i;
    }
    return -1;
}

START_TEST(wvlt_sincos_i16_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in_check;
    last_fn_name = NULL;

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    out[0] = sindata_etalon;
    out[1] = cosdata_etalon;
    (*get_fn(OPT_GENERIC, 0))(&pin, STREAM_SIZE_BZ, (void**)out, STREAM_SIZE_BZ);

    out[0] = sindata;
    out[1] = cosdata;

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out[0], 0, STREAM_SIZE_BZ);
            memset(out[1], 0, STREAM_SIZE_BZ);
            (*fn)(&pin, STREAM_SIZE_BZ, (void**)out, STREAM_SIZE_BZ);

            int res = is_equal();
            res >= 0 ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
#ifdef DEBUG_PRINT
            for(int i = res - 20; res >= 0 && i <= res + 20; ++i)
            {
                if(i >= 0 && i < WORD_COUNT)
                    fprintf(stderr, "%si#%d : in = %d, out = {sin:%d cos:%d}, etalon = {sin:%d cos:%d}, delta = {%d %d}\n",
                            i == res ? ">>>" : "   ",
                            i, in_check[i], sindata[i], cosdata[i], sindata_etalon[i], cosdata_etalon[i],
                            abs(sindata_etalon[i] - sindata[i]), abs(cosdata_etalon[i] - cosdata[i]));
            }
#endif
            ck_assert_int_eq( res, -1 );
        }
    }
}
END_TEST


START_TEST(wvlt_sincos_i16_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    out[0] = sindata;
    out[1] = cosdata;
    last_fn_name = NULL;

    const size_t bzin  = packet_lens[_i] * sizeof(int16_t);
    const size_t bzout = bzin;

    fprintf(stderr, "\n**** Compare SIMD implementations speed ***\n");
    fprintf(stderr,   "**** packet: %lu bytes, iters: %u ***\n", bzin, packet_lens[_i]);

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            //warming
            for(int i = 0; i < 10; ++i) (*fn)(&pin, 1000, (void**)out, 1000);

            //measuring
            uint64_t tk = clock_get_time();
            (*fn)(&pin, bzin, (void**)out, bzout);
            uint64_t tk1 = clock_get_time() - tk;
            fprintf(stderr, "\t%" PRIu64 " us elapsed, %" PRIu64 " ns per 1 call, ave speed = %" PRIu64 " calls/s \n",
                    tk1, (uint64_t)(tk1*1000LL/packet_lens[_i]), (uint64_t)(1000000LL*packet_lens[_i]/tk1));
        }
    }
}
END_TEST

Suite * wvlt_sincos_i16_suite(void)
{
    Suite *s;
    TCase *tc_core;

    max_opt = cpu_vcap_get();

    s = suite_create("wvlt_sincos_i16");
    tc_core = tcase_create("XDSP");
    tcase_set_timeout(tc_core, 60);
    tcase_add_unchecked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, wvlt_sincos_i16_check_simd);
    tcase_add_loop_test(tc_core, wvlt_sincos_i16_speed, 0, 3);

    suite_add_tcase(s, tc_core);
    return s;
}
