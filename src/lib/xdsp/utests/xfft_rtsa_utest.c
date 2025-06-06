// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include "xdsp_utest_common.h"
#include "../rtsa_functions.h"

#undef DEBUG_PRINT

#define LOWER_PWR_BOUND -120
#define UPPER_PWR_BOUND -0
#define DIVS_FOR_DB 1

#define AVGS 128
#define RAISE_COEF 32
#define DECAY_COEF 1
#define CHARGING_FRAME 256

#define STREAM_SIZE 4096
static_assert( STREAM_SIZE >= 4096, "STREAM_SIZE should be >= 4096!" );
static const unsigned packet_lens[4] = { 512, 1024, 2048, STREAM_SIZE };

#define SPEED_MEASURE_ITERS 256

#define EPSILON MAX_RTSA_PWR / 10

static const char* last_fn_name = NULL;
static generic_opts_t max_opt = OPT_GENERIC;

static wvlt_fftwf_complex* in;
static uint16_t* in16;
static rtsa_pwr_t* out = NULL;
static rtsa_pwr_t* out_etalon = NULL;

static struct fft_rtsa_data rtsa_data;
static struct fft_rtsa_data rtsa_data_etalon;

static float scale_mpy = 3.010f;
static float mine = 1E-7f;
static float corr = -50.f;

static fft_rtsa_settings_t rtsa_settings;
static rtsa_hwi16_consts_t hwi16_consts;

static void setup(void)
{
    int res = 0;
    res = res ? res : posix_memalign((void**)&in,   ALIGN_BYTES, sizeof(wvlt_fftwf_complex) * STREAM_SIZE * AVGS);
    res = res ? res : posix_memalign((void**)&in16, ALIGN_BYTES, sizeof(uint16_t) * STREAM_SIZE * AVGS);
    assert(res == 0);

    //init input data
    srand( time(0) );

    for(unsigned j = 0; j < AVGS; ++j)
    {
        wvlt_fftwf_complex* ptr = in + j * STREAM_SIZE;
        for(unsigned i = 0; i < STREAM_SIZE; ++i)
        {
            ptr[i][0] =  100.0f * (float)(rand()) / (float)RAND_MAX;
            ptr[i][1] = -100.0f * (float)(rand()) / (float)RAND_MAX;
        }

        uint16_t* ptr16 = in16 + j * STREAM_SIZE;
        for(unsigned i = 0; i < STREAM_SIZE; ++i)
        {
            ptr16[i] =  (uint16_t)(65535.f * (float)(rand()) / (float)RAND_MAX);
        }
    }

    fft_rtsa_settings_t * st = &rtsa_settings;

    st->lower_pwr_bound = LOWER_PWR_BOUND;
    st->upper_pwr_bound = UPPER_PWR_BOUND;
    st->divs_for_dB     = DIVS_FOR_DB;
    st->charging_frame  = CHARGING_FRAME;
    st->raise_coef      = RAISE_COEF;
    st->decay_coef      = DECAY_COEF;
    rtsa_calc_depth(st);

    rtsa_data.settings = rtsa_data_etalon.settings = rtsa_settings;

    res = 0;
    res = res ? res : posix_memalign((void**)&out,        ALIGN_BYTES, sizeof(rtsa_pwr_t) * STREAM_SIZE * st->rtsa_depth);
    res = res ? res : posix_memalign((void**)&out_etalon, ALIGN_BYTES, sizeof(rtsa_pwr_t) * STREAM_SIZE * st->rtsa_depth);
    assert(res == 0);
    memset(out       , 0, sizeof(rtsa_pwr_t) * STREAM_SIZE * st->rtsa_depth);
    memset(out_etalon, 0, sizeof(rtsa_pwr_t) * STREAM_SIZE * st->rtsa_depth);

    rtsa_data.pwr  = out;
    rtsa_data_etalon.pwr = out_etalon;

    scale_mpy = 10.0f / log2(10);
}

static void teardown(void)
{
    free(in);
    free(out);
    free(out_etalon);
}

static int32_t is_equal()
{
    for(unsigned i = 0; i < STREAM_SIZE * rtsa_settings.rtsa_depth; i++)
    {
        if(abs(out[i] - out_etalon[i]) > EPSILON) return i;
    }
    return -1;
}

START_TEST(rtsa_check)
{
    generic_opts_t opt = max_opt;
    fprintf(stderr,"\n**** Check SIMD implementations ***\n");

    fft_diap_t diap = {0, STREAM_SIZE};
    rtsa_fill_hwi16_consts(&rtsa_settings, STREAM_SIZE, scale_mpy, &hwi16_consts);

    // get reference
    rtsa_init(&rtsa_data_etalon, STREAM_SIZE);
    for(unsigned i = 0; i < AVGS; ++i)
    {
        wvlt_fftwf_complex* ptr = in + i * STREAM_SIZE;
        rtsa_update_c(OPT_GENERIC, NULL)
            (ptr, STREAM_SIZE, &rtsa_data_etalon, scale_mpy, mine, corr, diap);
    }

    last_fn_name = NULL;
    const char* fn_name = NULL;
    rtsa_update_function_t fn_update = NULL;

    while(opt != OPT_GENERIC)
    {
        fn_update = rtsa_update_c(opt, &fn_name);

        if(last_fn_name && !strcmp(last_fn_name, fn_name))
        {
            --opt;
            continue;
        }

        last_fn_name = fn_name;

        rtsa_init(&rtsa_data, STREAM_SIZE);
        for(unsigned i = 0; i < AVGS; ++i)
        {
            wvlt_fftwf_complex* ptr = in + i * STREAM_SIZE;
            (*fn_update)
                (ptr, STREAM_SIZE, &rtsa_data, scale_mpy, mine, corr, diap);
        }

        int res = is_equal();
        fprintf(stderr, "%-20s\t", fn_name);
        (res >= 0) ? fprintf(stderr, "\tFAILED!\n") : fprintf(stderr, "\tOK!\n");

#if 1
        if(res >= 0)
        {
            unsigned j = res >= 10 ? res - 10 : 0;

            for(; j <= res + 10 && j < STREAM_SIZE * rtsa_settings.rtsa_depth; ++j)
                fprintf(stderr, "%sTEST  > i:%u in=(%.6f,%.6f) out=%u <---> out_etalon=%u\n",
                        j == res ? ">>>>>>>>> " : "",
                        j, in[j][0], in[j][1], out[j], out_etalon[j]);

            exit(1);
        }
#endif
        ck_assert_int_eq( res, -1 );
        --opt;
    }
}
END_TEST

START_TEST(rtsa_speed)
{
    fprintf(stderr, "\n**** Compare SIMD implementations speed ***\n");

    const char* fn_name = NULL;
    rtsa_update_function_t fn_update = NULL;

    unsigned size = packet_lens[_i];
    fft_diap_t diap = {0, size};
    rtsa_fill_hwi16_consts(&rtsa_settings, _i, scale_mpy, &hwi16_consts);

    last_fn_name = NULL;
    generic_opts_t opt = max_opt;

    fprintf(stderr, "**** packet: %u elems, rtsa_depth = %u, averaging = %u, iters: %u ***\n", size, rtsa_settings.rtsa_depth, AVGS, SPEED_MEASURE_ITERS);

    while(opt != OPT_GENERIC)
    {
        fn_update = rtsa_update_c(opt, &fn_name);

        if(last_fn_name && !strcmp(last_fn_name, fn_name))
        {
            --opt;
            continue;
        }
        last_fn_name = fn_name;

        fprintf(stderr, "%-20s\t", fn_name);

        //warming
        rtsa_init(&rtsa_data, size);
        for(unsigned i = 0; i < 10000; ++i)
                (*fn_update)
                    (in, size, &rtsa_data, scale_mpy, mine, corr, diap);

        //measuring
        rtsa_init(&rtsa_data, size);
        uint64_t tk = clock_get_time();

        for(unsigned i = 0; i < SPEED_MEASURE_ITERS; ++i)
        {
            for(unsigned j = 0; j < AVGS; ++j)
            {
                wvlt_fftwf_complex* ptr = in + j * STREAM_SIZE;
                (*fn_update)
                    (ptr, size, &rtsa_data, scale_mpy, mine, corr, diap);
            }
        }

        uint64_t tk1 = clock_get_time() - tk;

        fprintf(stderr, "\t%" PRIu64 " us elapsed, %" PRIu64 " ns per 1 cycle, ave speed = %" PRIu64 " cycles/s \n",
                tk1,
                (uint64_t)(tk1*1000LL/SPEED_MEASURE_ITERS/AVGS),
                (uint64_t)(1000000LL*SPEED_MEASURE_ITERS*AVGS/tk1));

        --opt;
    }
}
END_TEST


START_TEST(rtsa_speed_u16)
{
    fprintf(stderr, "\n**** Compare SIMD implementations speed (pure u16) ***\n");

    const char* fn_name = NULL;
    rtsa_update_hwi16_function_t fn_update = NULL;

    unsigned size = packet_lens[_i];
    fft_diap_t diap = {0, size};

    scale_mpy /= HWI16_SCALE_COEF;
    corr = corr / HWI16_SCALE_COEF + HWI16_CORR_COEF;
    rtsa_fill_hwi16_consts(&rtsa_settings, _i, scale_mpy, &hwi16_consts);

    last_fn_name = NULL;
    generic_opts_t opt = max_opt;

    fprintf(stderr, "**** packet: %u elems, rtsa_depth = %u, averaging = %u, iters: %u ***\n", size, rtsa_settings.rtsa_depth, AVGS, SPEED_MEASURE_ITERS);

    while(opt != OPT_GENERIC)
    {
        fn_update = rtsa_update_hwi16_c(opt, &fn_name);

        if(last_fn_name && !strcmp(last_fn_name, fn_name))
        {
            --opt;
            continue;
        }
        last_fn_name = fn_name;

        fprintf(stderr, "%-20s\t", fn_name);

        //warming
        rtsa_init(&rtsa_data, size);
        for(unsigned i = 0; i < 10000; ++i)
            (*fn_update)
                (in16, size, &rtsa_data, scale_mpy, corr, diap, &hwi16_consts);

        //measuring
        rtsa_init(&rtsa_data, size);
        uint64_t tk = clock_get_time();

        for(unsigned i = 0; i < SPEED_MEASURE_ITERS; ++i)
        {
            for(unsigned j = 0; j < AVGS; ++j)
            {
                uint16_t* ptr = in16 + j * STREAM_SIZE;
                (*fn_update)
                    (ptr, size, &rtsa_data, scale_mpy, corr, diap, &hwi16_consts);
            }
        }

        uint64_t tk1 = clock_get_time() - tk;

        fprintf(stderr, "\t%" PRIu64 " us elapsed, %" PRIu64 " ns per 1 cycle, ave speed = %" PRIu64 " cycles/s \n",
                tk1,
                (uint64_t)(tk1*1000LL/SPEED_MEASURE_ITERS/AVGS),
                (uint64_t)(1000000LL*SPEED_MEASURE_ITERS*AVGS/tk1));

        --opt;
    }
}
END_TEST




Suite * rtsa_suite(void)
{
    Suite *s;
    TCase *tc_core;

    max_opt = cpu_vcap_get();

    s = suite_create("xfft_rtsa_functions");
    tc_core = tcase_create("XFFT");
    tcase_set_timeout(tc_core, 300);
    tcase_add_unchecked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, rtsa_check);
    //tcase_add_loop_test(tc_core, rtsa_speed, 0, 4);
    tcase_add_loop_test(tc_core, rtsa_speed_u16, 0, 4);
    suite_add_tcase(s, tc_core);
    return s;
}


