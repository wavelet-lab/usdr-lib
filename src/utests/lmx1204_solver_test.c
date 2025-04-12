#include <stdio.h>
#include <check.h>
#include "lmx1204/lmx1204.h"

static lmx1204_state_t st;

static void setup()
{
    memset(&st, 0, sizeof(st));

    st.ch_en[0] = 1;
    st.ch_en[1] = 1;
    st.ch_en[2] = 1;
    st.ch_en[3] = 1;

    st.clkout_en[0] = 1;
    st.clkout_en[1] = 1;
    st.clkout_en[2] = 1;
    st.clkout_en[3] = 1;

    st.sysref_en = 1;
    st.sysrefout_en[0] = 1;
    st.sysrefout_en[1] = 1;
    st.sysrefout_en[2] = 1;
    st.sysrefout_en[3] = 1;

    st.logic_en = 1;
    st.logiclkout_en = 1;
    st.logisysrefout_en = 1;

    st.logiclkout_fmt = LMX1204_FMT_LVDS;
    st.logisysrefout_fmt = LMX1204_FMT_LVDS;
}

static void teardown() {}

START_TEST(lmx1204_solver_test1)
{
    st.clkin = 12800000000;
    st.clkout = st.clkin;

    st.sysrefreq = 1000000;
    st.sysrefout = st.sysrefreq;
    st.sysref_mode = LMX1204_REPEATER;

    st.logiclkout = 400000000;

    int res = lmx1204_solver(&st, false, true);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx1204_solver_test2)
{
    st.clkin = 12800000000;
    st.clkout = st.clkin;

    st.sysrefreq = 0;
    st.sysrefout = 40000000;
    st.sysref_mode = LMX1204_CONTINUOUS;

    st.logiclkout = 8000000;

    int res = lmx1204_solver(&st, false, true);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx1204_solver_test3)
{
    st.clkin = 6400000000;
    st.clkout = st.clkin;
    st.filter_mode = 1;

    st.sysrefreq = 0;
    st.sysrefout = 20000000;
    st.sysref_mode = LMX1204_CONTINUOUS;

    st.logiclkout = 4000000;

    int res = lmx1204_solver(&st, false, true);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmx1204_solver_test4)
{
    st.clkin = 1400000000;
    st.clkout = st.clkin * 4;

    st.sysrefreq = 0;
    st.sysrefout = 4375000;
    st.sysref_mode = LMX1204_CONTINUOUS;

    st.logiclkout = 1400000;

    int res = lmx1204_solver(&st, false, true);
    ck_assert_int_eq( res, 0 );
}

Suite * lmx1204_solver_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("lmx1204_solver");
    tc_core = tcase_create("HW");
    tcase_set_timeout(tc_core, 1);
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, lmx1204_solver_test1);
    tcase_add_test(tc_core, lmx1204_solver_test2);
    tcase_add_test(tc_core, lmx1204_solver_test3);
    tcase_add_test(tc_core, lmx1204_solver_test4);

    suite_add_tcase(s, tc_core);
    return s;
}

