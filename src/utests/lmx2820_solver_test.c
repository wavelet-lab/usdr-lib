#include <stdio.h>
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

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test2)
{
    const uint64_t osc_in = 1400000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 45000000;
    uint64_t out_freq2 = out_freq1;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test3)
{
    const uint64_t osc_in = 5000000;
    const int mash_order = 0;
    uint64_t out_freq1 = 22600000000ull;
    uint64_t out_freq2 = out_freq1;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test4)
{
    const uint64_t osc_in = 1400000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 22600000000ull;
    uint64_t out_freq2 = out_freq1;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test5)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 5600000000ull;
    uint64_t out_freq2 = out_freq1 >> 3;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test6)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 5600000000ull;
    uint64_t out_freq2 = out_freq1 << 1;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test7)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 5800000000ull;
    uint64_t out_freq2 = out_freq1 << 1;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test8)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 2000098000ull;
    uint64_t out_freq2 = out_freq1 >> 4;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test9_force_mult)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 20000988000ull;
    uint64_t out_freq2 = out_freq1 >> 4;

    int res = lmx2820_solver(&st, osc_in, mash_order, _i, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test10_mash_order)
{
    const uint64_t osc_in = 250000000ull;
    uint64_t out_freq1 = 5600000000ull;
    uint64_t out_freq2 = out_freq1 >> 3;

    int res = lmx2820_solver(&st, osc_in, _i, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test11_mash_order)
{
    const uint64_t osc_in = 1400000000ull;
    uint64_t out_freq1 = 45000000;
    uint64_t out_freq2 = out_freq1;

    int res = lmx2820_solver(&st, osc_in, _i, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test12_instcal)
{
    const uint64_t osc_in = 1400000000ull;
    const int mash_order = 0;
    uint64_t out_freq1 = 45000000ull << 8;
    uint64_t out_freq2 = out_freq1 >> 3;

    int res = lmx2820_solver(&st, osc_in, 2, 0, 5650000000ull, 5650000000ull);
    ck_assert_int_eq( res, 0 );

    fprintf(stderr, "Calibrating for INSTCAL, fixing at FPD:%.2f\n", st.lmx2820_input_chain.fpd);

    res = lmx2820_solver_instcal(&st, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test13_pesync)
{
    const uint64_t osc_in = 250000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 320000000ull;
    uint64_t out_freq2 = 160000000ull;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx2820_solver_test14_pesync)
{
    const uint64_t osc_in = 25000000ull;
    const int mash_order = 2;
    uint64_t out_freq1 = 4000000000ull;
    uint64_t out_freq2 = 4000000000ull;

    st.lmx2820_sysref_chain.enabled = true;
    st.lmx2820_sysref_chain.srout = 25000000;
    st.lmx2820_sysref_chain.master_mode = true;
    st.lmx2820_sysref_chain.cont_pulse = true;

    int res = lmx2820_solver(&st, osc_in, mash_order, 0, out_freq1, out_freq2);
    ck_assert_int_eq( res, 0 );
}
END_TEST


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
    tcase_add_loop_test(tc_core, lmx2820_solver_test9_force_mult, 3, 8);
    tcase_add_loop_test(tc_core, lmx2820_solver_test10_mash_order, 1, 4);
    tcase_add_loop_test(tc_core, lmx2820_solver_test11_mash_order, 1, 4);
    tcase_add_test(tc_core, lmx2820_solver_test12_instcal);
    tcase_add_test(tc_core, lmx2820_solver_test13_pesync);
    tcase_add_test(tc_core, lmx2820_solver_test14_pesync);

    suite_add_tcase(s, tc_core);
    return s;
}
