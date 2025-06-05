// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "conv_i12_i16_2.h"

#undef DEBUG_PRINT

#define IN_STREAM_SIZE_BZ (132u)                    // (6 + 3 + 2)*12 = 132 bytes
#define WORD_COUNT (IN_STREAM_SIZE_BZ * 8u / 12u)   // 88 i12 words

#define SPEED_WORD_COUNT (8192u)
#define SPEED_SIZE_BZ (SPEED_WORD_COUNT * 12u / 8u)

static const unsigned packet_lens[3] = { 1235, 7777, SPEED_SIZE_BZ };

#define SPEED_MEASURE_ITERS 1000000

static int16_t* in = NULL;
static int16_t* out = NULL;
static int16_t* out_etalon = NULL;

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static void setup()
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in,         ALIGN_BYTES, SPEED_SIZE_BZ);
    res = res ? res : posix_memalign((void**)&out,        ALIGN_BYTES, sizeof(int16_t) * SPEED_WORD_COUNT);
    res = res ? res : posix_memalign((void**)&out_etalon, ALIGN_BYTES, sizeof(int16_t) * SPEED_WORD_COUNT);
    ck_assert_int_eq(res, 0);

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
    conv_function_t fn = conv_get_i12_i16_c(o, &fn_name);

    //ignore dups
    if(last_fn_name && !strcmp(last_fn_name, fn_name))
        return NULL;

    if(log)
        fprintf(stderr, "%-20s\t", fn_name);

    last_fn_name = fn_name;
    return fn;
}

START_TEST(conv_i12_i16_check)
{
    last_fn_name = NULL;

    const void* pin = (const void*)in;
    void* pout = (void*)out;

    const size_t bzin  = IN_STREAM_SIZE_BZ;
    const size_t bzout = WORD_COUNT * sizeof(int16_t);

    fprintf(stderr,"\n**** Check generic ***\n");

    (*get_fn(OPT_GENERIC, 1))(&pin, bzin, &pout, bzout);

    for(uint16_t i = 0; i < WORD_COUNT; ++i)
    {
        int16_t v = (i << 4);
        v = (i % 4) ? v : -v;

        if(v != out[i])
            fprintf(stderr, "\ni=%u\tout=%d\texpected=%d", i, out[i], v);
        ck_assert(v == out[i]);
    }
    fprintf(stderr, "\n");
}
END_TEST

START_TEST(conv_i12_i16_check_simd)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void* pout = (void*)out;
    last_fn_name = NULL;

    const size_t bzin  = SPEED_SIZE_BZ;
    const size_t bzout = SPEED_WORD_COUNT * sizeof(int16_t);

    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    //get etalon output data (generic foo)
    (*get_fn(OPT_GENERIC, 0))(&pin, bzin, &pout, bzout);
    memcpy(out_etalon, out, bzout);

#ifdef DEBUG_PRINT
    fprintf(stderr, "ETALON:\n");
    for(uint16_t i = 0; i < 16; ++i)
    {
        fprintf(stderr, "%d ", out[i] / 16);
    }
    fprintf(stderr, "\n");
#endif

    while(opt != OPT_GENERIC)
    {
        conv_function_t fn = get_fn(opt--, 1);
        if(fn)
        {
            memset(out, 0, bzout);
            (*fn)(&pin, bzin, &pout, bzout);
#ifdef DEBUG_PRINT
            fprintf(stderr, "\n");
            for(uint16_t i = 0; i < 16; ++i)
            {
                fprintf(stderr, "%d ", out[i] / 16);
            }
            fprintf(stderr, "\n");
#endif
            int res = memcmp(out, out_etalon, bzout);
            res ? fprintf(stderr,"\tFAILED!\n") : fprintf(stderr,"\tOK!\n");
#ifdef DEBUG_PRINT
            for(int i = 0; res && i < 32; ++i)
            {
                fprintf(stderr, "i = %d : out = %d, etalon = %d\n", i, out[i] / 16, out_etalon[i] / 16);
            }
#endif
            ck_assert_int_eq( res, 0 );
        }
    }
}
END_TEST


START_TEST(conv_i12_i16_speed)
{
    generic_opts_t opt = max_opt;
    conv_function_t fn = NULL;
    const void* pin = (const void*)in;
    void* pout = (void*)out;
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

Suite * conv_i12_i16_suite(void)
{
    max_opt = cpu_vcap_get();

    Suite* s = suite_create("conv_i12_i16");

    ADD_REGRESS_TEST(s, conv_i12_i16_check);
    ADD_REGRESS_TEST(s, conv_i12_i16_check_simd);
    ADD_PERF_LOOP_TEST(s, conv_i12_i16_speed, 60, 0, 3);

    return s;
}
