#include <check.h>
#include "lmx2820/lmx2820.h"

static lmx2820_state_t st;

static void setup()
{
    memset(&st, 0, sizeof(st));
}

static void teardown() {}

START_TEST(lmx2820_solver_test1)
{
    const uint64_t osc_in = 5000000;
    const int mash_order = 0;
    uint64_t out_freq1 = 45000000;
    uint64_t out_freq2 = out_freq1;

    int res = lmx2820_solver(&st, osc_in, mash_order, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx2820_solver_test2)
{
    const uint64_t osc_in = 1400000000ull;
    const int mash_order = 0;
    uint64_t out_freq1 = 45000000;
    uint64_t out_freq2 = out_freq1;

    int res = lmx2820_solver(&st, osc_in, mash_order, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx2820_solver_test3)
{
    const uint64_t osc_in = 5000000;
    const int mash_order = 0;
    uint64_t out_freq1 = 22600000000ull;
    uint64_t out_freq2 = out_freq1;

    int res = lmx2820_solver(&st, osc_in, mash_order, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}


START_TEST(lmx2820_solver_test4)
{
    const uint64_t osc_in = 1400000000ull;
    const int mash_order = 0;
    uint64_t out_freq1 = 22600000000ull;
    uint64_t out_freq2 = out_freq1;

    int res = lmx2820_solver(&st, osc_in, mash_order, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx2820_solver_test5)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 0;
    uint64_t out_freq1 = 5600000000ull;
    uint64_t out_freq2 = out_freq1 >> 3;

    int res = lmx2820_solver(&st, osc_in, mash_order, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx2820_solver_test6)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 0;
    uint64_t out_freq1 = 5600000000ull;
    uint64_t out_freq2 = out_freq1 << 1;

    int res = lmx2820_solver(&st, osc_in, mash_order, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx2820_solver_test7)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 0;
    uint64_t out_freq1 = 5800000000ull;
    uint64_t out_freq2 = out_freq1 << 1;

    int res = lmx2820_solver(&st, osc_in, mash_order, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx2820_solver_test8)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 0;
    uint64_t out_freq1 = 20000987000ull;
    uint64_t out_freq2 = out_freq1 >> 4;

    int res = lmx2820_solver(&st, osc_in, mash_order, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}

Suite * lmx2820_solver_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("lmx2820_solver");
    tc_core = tcase_create("HW");
    tcase_set_timeout(tc_core, 1);
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, lmx2820_solver_test1);
    tcase_add_test(tc_core, lmx2820_solver_test2);
    tcase_add_test(tc_core, lmx2820_solver_test3);
    tcase_add_test(tc_core, lmx2820_solver_test4);
    tcase_add_test(tc_core, lmx2820_solver_test5);
    tcase_add_test(tc_core, lmx2820_solver_test6);
    tcase_add_test(tc_core, lmx2820_solver_test7);
    tcase_add_test(tc_core, lmx2820_solver_test8);

    suite_add_tcase(s, tc_core);
    return s;
}
