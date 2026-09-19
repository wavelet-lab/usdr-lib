// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_ci16_3ci16_2.h"

#undef DEBUG_PRINT

#define WORD_COUNT (4098u)
#define IN_STREAM_SIZE_BZ (WORD_COUNT * sizeof(int16_t))

#define SPEED_WORD_COUNT (32768u)
#define SPEED_SIZE_BZ (SPEED_WORD_COUNT * sizeof(int16_t))

static const unsigned packet_lens[3] = { 1024, 16384, SPEED_SIZE_BZ };

#define SPEED_MEASURE_ITERS 1000000

static int16_t* in = NULL;
static int16_t* out1 = NULL;
static int16_t* out1_etalon = NULL;
static int16_t* out2 = NULL;
static int16_t* out2_etalon = NULL;
static int16_t* out3 = NULL;
static int16_t* out3_etalon = NULL;

static int16_t* out[3] = {NULL, NULL, NULL};

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in,          ALIGN_BYTES, SPEED_SIZE_BZ);
    res = res ? res : posix_memalign((void**)&out1,        ALIGN_BYTES, sizeof(int16_t) * SPEED_WORD_COUNT/3);
    res = res ? res : posix_memalign((void**)&out1_etalon, ALIGN_BYTES, sizeof(int16_t) * SPEED_WORD_COUNT/3);
    res = res ? res : posix_memalign((void**)&out2,        ALIGN_BYTES, sizeof(int16_t) * SPEED_WORD_COUNT/3);
    res = res ? res : posix_memalign((void**)&out2_etalon, ALIGN_BYTES, sizeof(int16_t) * SPEED_WORD_COUNT/3);
    res = res ? res : posix_memalign((void**)&out3,        ALIGN_BYTES, sizeof(int16_t) * SPEED_WORD_COUNT/3);
    res = res ? res : posix_memalign((void**)&out3_etalon, ALIGN_BYTES, sizeof(int16_t) * SPEED_WORD_COUNT/3);
    ck_assert_int_eq(res, 0);

    out[0] = out1;
    out[1] = out2;
    out[2] = out3;

    srand( time(0) );

    //fill
    int16_t n = 0;
    for(unsigned i = 0; i < SPEED_WORD_COUNT; ++i)
    {
        int sign = (float)(rand()) / (float)RAND_MAX > 0.5 ? -1 : 1;
        in[i] = sign * 32767 * (float)(rand()) / (float)RAND_MAX;
        //in[i] = i;
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
}

static conv_function_t get_fn(generic_opts_t o, int log)
{
    return generic_get_fn(o, log, conv_get_ci16_3ci16_c, &last_fn_name);
}

static void print_data(const char* header)
{
    if(header)
        fprintf(stderr, "%s:", header);

    fprintf(stderr, "\n in: ");
    for(unsigned i = 0; i < 24; ++i)
    {
        fprintf(stderr, "%d ", in[i]);
    }

    for(unsigned n = 0; n < 3; ++n)
    {
        fprintf(stderr, "\n out%d: ", n);
        for(unsigned i = 0; i < 8; ++i)
        {
            fprintf(stderr, "%d ", out[n][i]);
        }
    }
    fprintf(stderr, "\n");
}

#define CONV_SCALE (1.0f/32767)

START_TEST(conv_ci16_3ci16_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void** pout = (void**)out;
    last_fn_name = NULL;

    const size_t bzin  = IN_STREAM_SIZE_BZ;
    const size_t bzout = WORD_COUNT * sizeof(int16_t);

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    memset(out[0], 0, bzout / 3);
    memset(out[1], 0, bzout / 3);
    memset(out[2], 0, bzout / 3);
    (*get_fn(OPT_GENERIC, 0))(&pin, bzin, pout, bzout);
    memcpy(out1_etalon, out[0], bzout / 3);
    memcpy(out2_etalon, out[1], bzout / 3);
    memcpy(out3_etalon, out[2], bzout / 3);

#ifdef DEBUG_PRINT
    print_data("ETALON");
#endif

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out[0], 0, bzout / 3);
            memset(out[1], 0, bzout / 3);
            memset(out[2], 0, bzout / 3);
            (*fn)(&pin, bzin, pout, bzout);

            int res = memcmp(out[0], out1_etalon, bzout / 3) ||
                      memcmp(out[1], out2_etalon, bzout / 3) ||
                      memcmp(out[2], out3_etalon, bzout / 3);

            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");

#ifdef DEBUG_PRINT
            print_data("TEST");
#endif
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_ci16_3ci16_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void** pout = (void**)out;
    last_fn_name = NULL;

    const size_t bzin  = packet_lens[_i];
    const size_t bzout = SPEED_WORD_COUNT * sizeof(int16_t);

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

Suite * conv_ci16_3ci16_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("conv_ci16_3ci16");

    ADD_REGRESS_TEST(s, conv_ci16_3ci16_check_simd);
    ADD_PERF_LOOP_TEST(s, conv_ci16_3ci16_speed, 60, 0, 3);

    return s;
}
