// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_ci16_4ci16_2.h"

#define DEBUG_PRINT

#define CHECK_WORD_COUNT (4096u + 77u)
#define CHECK_SIZE_BZ (CHECK_WORD_COUNT * sizeof(int16_t))

#define SPEED_WORD_COUNT (65536u)
#define SPEED_SIZE_BZ (SPEED_WORD_COUNT * sizeof(int16_t))

static const unsigned packet_lens[4] = { 8192u, 16384u, 32768u, SPEED_WORD_COUNT };

#define SPEED_MEASURE_ITERS 1000000

static int16_t* in = NULL;
static int16_t* out1 = NULL;
static int16_t* out1_etalon = NULL;
static int16_t* out2 = NULL;
static int16_t* out2_etalon = NULL;
static int16_t* out3 = NULL;
static int16_t* out3_etalon = NULL;
static int16_t* out4 = NULL;
static int16_t* out4_etalon = NULL;
static int16_t* out[4] = {NULL, NULL, NULL, NULL};

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    posix_memalign((void**)&in,          ALIGN_BYTES, SPEED_SIZE_BZ);

    posix_memalign((void**)&out1,        ALIGN_BYTES, SPEED_SIZE_BZ/4);
    posix_memalign((void**)&out2,        ALIGN_BYTES, SPEED_SIZE_BZ/4);
    posix_memalign((void**)&out3,        ALIGN_BYTES, SPEED_SIZE_BZ/4);
    posix_memalign((void**)&out4,        ALIGN_BYTES, SPEED_SIZE_BZ/4);

    posix_memalign((void**)&out1_etalon, ALIGN_BYTES, CHECK_SIZE_BZ/4);
    posix_memalign((void**)&out2_etalon, ALIGN_BYTES, CHECK_SIZE_BZ/4);
    posix_memalign((void**)&out3_etalon, ALIGN_BYTES, CHECK_SIZE_BZ/4);
    posix_memalign((void**)&out4_etalon, ALIGN_BYTES, CHECK_SIZE_BZ/4);

    out[0] = out1;
    out[1] = out2;
    out[2] = out3;
    out[3] = out4;

    srand( time(0) );

    //fill
    for(unsigned i = 0; i < SPEED_WORD_COUNT; ++i)
    {
        int sign = (float)(rand()) / (float)RAND_MAX > 0.5 ? -1 : 1;
        //in[i] = sign * 32767 * ((float)(rand()) / (float)RAND_MAX);
        in[i] = sign * i;
    }
}

static void teardown()
{
    free(in);
    free(out1);
    free(out2);
    free(out3);
    free(out4);
    free(out1_etalon);
    free(out2_etalon);
    free(out3_etalon);
    free(out4_etalon);
}

static conv_function_t get_fn(generic_opts_t o, int log)
{
    const char* fn_name = NULL;
    conv_function_t fn = conv_get_ci16_4ci16_c(o, &fn_name);

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

    fprintf(stderr, "\n in: ");
    for(unsigned i = 0; i < 32; ++i)
    {
        fprintf(stderr, "%d ", in[i]);
    }

    for(unsigned n = 0; n < 4; ++n)
    {
        fprintf(stderr, "\n out%d: ", n);
        for(unsigned i = 0; i < 8; ++i)
        {
            fprintf(stderr, "%4d ", out[n][i]);
        }
    }
    fprintf(stderr, "\n");
}

START_TEST(conv_ci16_4ci16_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void** pout = (void**)out;
    last_fn_name = NULL;

    const size_t bzin  = CHECK_SIZE_BZ;
    const size_t bzout = CHECK_SIZE_BZ;

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    (*get_fn(OPT_GENERIC, 0))(&pin, bzin, pout, bzout);
    print_data("ETALON");

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
#ifdef DEBUG_PRINT
            print_data(0);
#endif
            int res = memcmp(out[0], out1_etalon, bzout / 4) || memcmp(out[1], out2_etalon, bzout / 4) ||
                      memcmp(out[2], out3_etalon, bzout / 4) || memcmp(out[3], out4_etalon, bzout / 4);
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_ci16_4ci16_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void** pout = (void**)out;
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

Suite * conv_ci16_4ci16_suite(void)
{
    Suite *s;
    TCase *tc_core;

    max_opt = cpu_vcap_get();

    s = suite_create("conv_ci16_4ci16");
    tc_core = tcase_create("XDSP");
    tcase_set_timeout(tc_core, 60);
    tcase_add_unchecked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, conv_ci16_4ci16_check_simd);
    tcase_add_loop_test(tc_core, conv_ci16_4ci16_speed, 0, 4);

    suite_add_tcase(s, tc_core);
    return s;
}
