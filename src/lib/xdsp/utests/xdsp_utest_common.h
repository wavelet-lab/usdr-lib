// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef XDSP_UTEST_COMMON_H
#define XDSP_UTEST_COMMON_H

#include <inttypes.h>
#include <math.h>
#include "../../cal/opt_func.h"
#include "conv.h"

#define ALIGN_BYTES (size_t)64

#define ADD_REGRESS_TEST(suitename, testname) \
{ \
    TCase* tc_regress = tcase_create("REGRESS"); \
    tcase_set_tags(tc_regress, "REGRESS"); \
    tcase_add_unchecked_fixture(tc_regress, setup, teardown); \
    tcase_add_test(tc_regress, testname); \
    suite_add_tcase(suitename, tc_regress); \
}

#define ADD_PERF_LOOP_TEST(suitename, testname, timeout, from, to) \
{ \
    TCase* tc_perf = tcase_create("PERFORMANCE"); \
    tcase_set_tags(tc_perf, "PERFORMANCE"); \
    tcase_add_unchecked_fixture(tc_perf, setup, teardown); \
    tcase_set_timeout(tc_perf, timeout); \
    tcase_add_loop_test(tc_perf, testname, from, to); \
    suite_add_tcase(suitename, tc_perf); \
}

#define ADD_PERF_TEST(suitename, testname, timeout) \
{ \
    TCase* tc_perf = tcase_create("PERFORMANCE"); \
    tcase_set_tags(tc_perf, "PERFORMANCE"); \
    tcase_add_unchecked_fixture(tc_perf, setup, teardown); \
    tcase_set_timeout(tc_perf, timeout); \
    tcase_add_test(tc_perf, testname); \
    suite_add_tcase(suitename, tc_perf); \
}

typedef conv_function_t (*conv_wrapper_fn_t)(generic_opts_t cpu_cap, const char** sfunc);

#endif // XDSP_UTEST_COMMON_H
