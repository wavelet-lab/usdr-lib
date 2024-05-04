// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

// for fsincos
#define _GNU_SOURCE

#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "vbase.h"
#include "trig.h"
#include "trig_inline.h"

#define ERROR 5

START_TEST(test_isincos_sincos) {
    int16_t phase = (int16_t) _i;
    float fphase = (float) phase * (float) M_PI / 2 / 32768;
    float fs, fc;
    sincosf(fphase, &fs, &fc);

    int fsi = (int) (fs * 32767);
    int fci = (int) (fc * 32767);
    int16_t os, oc;

    isincos_gen86(&phase, &os, &oc);

    ck_assert_int_lt(os, fsi + ERROR);
    ck_assert_int_gt(os, fsi - ERROR);
    ck_assert_int_lt(oc, fci + ERROR);
    ck_assert_int_gt(oc, fci - ERROR);
}
END_TEST

START_TEST(test_speedup) {
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    float cfs = 0;
    float fs, fc;
    for (int i = -32768; i < 32768; i++) {
        float fphase = (float) i * (float) M_PI / 2 / 32768;
        sincosf(fphase, &fs, &fc);
        cfs += fs;
    }
    clock_gettime(CLOCK_MONOTONIC, &b);

    int ifs = (int) (cfs * 32767);
    ck_assert_int_lt(ifs, -32767 + 3 * ERROR);
    ck_assert_int_gt(ifs, -32767 - 3 * ERROR);

    long delta_us = (b.tv_nsec - a.tv_nsec + (b.tv_sec - a.tv_sec) * 1000000000) / 1000;
    fprintf(stderr, "Exec test_speedup took %d us\n", (int) delta_us);
}
END_TEST

#if defined (__x86_64) || defined (__x86)
__attribute__((optimize("O3"), target("ssse3")))
START_TEST(test_speedup_ssse3) {
    struct timespec a, b;
    int ifs;
    clock_gettime(CLOCK_MONOTONIC, &a);
    __m128i pd = _mm_set_epi16(7, 6, 5, 4, 3, 2, 1, 0);
    __m128i pi = _mm_set1_epi16(8);
    __m128i phase = _mm_set1_epi16(-32768);

    __m128i vsin, vcos;
    __m128i csin = _mm_set1_epi16(0);

    for (int p = 0; p < 65536 / 8; p++) {
        phase = _mm_add_epi16(phase, pd);
        isincos_ssse3(&phase, &vsin, &vcos);
        csin = _mm_add_epi16(csin, vsin);
    }

    clock_gettime(CLOCK_MONOTONIC, &b);

    long delta_us = (b.tv_nsec - a.tv_nsec + (b.tv_sec - a.tv_sec) * 1000000000) / 1000;
    fprintf(stderr, "Exec test_speedup_ssse3 took %d us\n", (int) delta_us);
}
END_TEST

__attribute__((optimize("O3"), target("ssse3")))
START_TEST(test_isincos_ssse3) {
    int16_t phase = (int16_t) _i;
    __m128i vph = _mm_set1_epi16(phase);
    __m128i vsin, vcos;

    isincos_ssse3(&vph, &vsin, &vcos);

    int16_t os, oc;
    isincos_gen86(&phase, &os, &oc);

    ck_assert_int_eq(os, (int16_t) _mm_extract_epi16(vsin, 0));
    ck_assert_int_eq(os, (int16_t) _mm_extract_epi16(vsin, 1));
    ck_assert_int_eq(os, (int16_t) _mm_extract_epi16(vsin, 2));
    ck_assert_int_eq(os, (int16_t) _mm_extract_epi16(vsin, 3));
    ck_assert_int_eq(os, (int16_t) _mm_extract_epi16(vsin, 4));
    ck_assert_int_eq(os, (int16_t) _mm_extract_epi16(vsin, 5));
    ck_assert_int_eq(os, (int16_t) _mm_extract_epi16(vsin, 6));
    ck_assert_int_eq(os, (int16_t) _mm_extract_epi16(vsin, 7));

    ck_assert_int_eq(oc, (int16_t) _mm_extract_epi16(vcos, 0));
    ck_assert_int_eq(oc, (int16_t) _mm_extract_epi16(vcos, 1));
    ck_assert_int_eq(oc, (int16_t) _mm_extract_epi16(vcos, 2));
    ck_assert_int_eq(oc, (int16_t) _mm_extract_epi16(vcos, 3));
    ck_assert_int_eq(oc, (int16_t) _mm_extract_epi16(vcos, 4));
    ck_assert_int_eq(oc, (int16_t) _mm_extract_epi16(vcos, 5));
    ck_assert_int_eq(oc, (int16_t) _mm_extract_epi16(vcos, 6));
    ck_assert_int_eq(oc, (int16_t) _mm_extract_epi16(vcos, 7));
}
END_TEST

__attribute__((optimize("O3"), target("avx2")))
START_TEST(test_speedup_avx2) {
    struct timespec a, b;
    int ifs;
    clock_gettime(CLOCK_MONOTONIC, &a);
    __m256i pd = _mm256_set_epi16(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    __m256i pi = _mm256_set1_epi16(8);
    __m256i phase = _mm256_set1_epi16(-32768);

    __m256i vsin, vcos;
    __m256i csin = _mm256_set1_epi16(0);

    for (int p = 0; p < 65536 / 16; p++) {
        phase = _mm256_add_epi16(phase, pd);
        isincos_avx2(&phase, &vsin, &vcos);
        csin = _mm256_add_epi16(csin, vsin);
    }

    clock_gettime(CLOCK_MONOTONIC, &b);

    long delta_us = (b.tv_nsec - a.tv_nsec + (b.tv_sec - a.tv_sec) * 1000000000) / 1000;
    fprintf(stderr, "Exec test_speedup_avx2 took %d us\n", (int) delta_us);
}
END_TEST

__attribute__((optimize("O3"), target("avx2")))
START_TEST(test_isincos_avx2) {
    int16_t phase = (int16_t) _i;
    __m256i vph = _mm256_set1_epi16(phase);
    __m256i vsin, vcos;

    isincos_avx2(&vph, &vsin, &vcos);

    int16_t os, oc;
    isincos_gen86(&phase, &os, &oc);

    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 0));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 1));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 2));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 3));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 4));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 5));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 6));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 7));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 8));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 9));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 10));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 11));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 12));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 13));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 14));
    ck_assert_int_eq(os, (int16_t) _mm256_extract_epi16(vsin, 15));


    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 0));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 1));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 2));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 3));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 4));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 5));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 6));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 7));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 8));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 9));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 10));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 11));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 12));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 13));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 14));
    ck_assert_int_eq(oc, (int16_t) _mm256_extract_epi16(vcos, 15));
}
END_TEST
#endif

#define PHASE_START -32768
#define PHASE_STOP  -32767

Suite * trig_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Trig");
    tc_core = tcase_create("Core");

    tcase_add_loop_test(tc_core, test_isincos_sincos, PHASE_START, PHASE_STOP);

#if defined (__x86_64) || defined (__x86)
    if (cpu_vcap_get() >= OPT_SSSE3) {
        tcase_add_loop_test(tc_core, test_isincos_ssse3, PHASE_START, PHASE_STOP);
        tcase_add_test(tc_core, test_speedup_ssse3);
    } else {
        fprintf(stderr, "Ignored test_isincos_ssse3 due to lack of SSSE3\n");
    }

    if (cpu_vcap_get() >= OPT_AVX2) {
        tcase_add_loop_test(tc_core, test_isincos_avx2, PHASE_START, PHASE_STOP);
        tcase_add_test(tc_core, test_speedup_avx2);
    } else {
        fprintf(stderr, "Ignored test_isincos_avx2 due to lack of AVX2\n");
    }
#endif

    tcase_add_test(tc_core, test_speedup);
    suite_add_tcase(s, tc_core);
    return s;
}
