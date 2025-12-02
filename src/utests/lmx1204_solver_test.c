#include <stdio.h>
#include <check.h>
#include "lmx1204/lmx1204.h"

static lmx1204_state_t st;

static void setup()
{
    memset(&st, 0, sizeof(st));

    st.ch_en[LMX1204_CH0] = 1;
    st.ch_en[LMX1204_CH1] = 1;
    st.ch_en[LMX1204_CH2] = 1;
    st.ch_en[LMX1204_CH3] = 1;
    st.ch_en[LMX1204_CH_LOGIC] = 1;

    st.clkout_en[LMX1204_CH0] = 1;
    st.clkout_en[LMX1204_CH1] = 1;
    st.clkout_en[LMX1204_CH2] = 1;
    st.clkout_en[LMX1204_CH3] = 1;
    st.clkout_en[LMX1204_CH_LOGIC] = 1;

    st.sysref_en = 1;
    st.sysrefout_en[LMX1204_CH0] = 1;
    st.sysrefout_en[LMX1204_CH1] = 1;
    st.sysrefout_en[LMX1204_CH2] = 1;
    st.sysrefout_en[LMX1204_CH3] = 1;
    st.sysrefout_en[LMX1204_CH_LOGIC] = 1;

    st.logiclkout_fmt = LMX1204_FMT_LVDS;
    st.logisysrefout_fmt = LMX1204_FMT_LVDS;

    lmx1204_init_sysrefout_ch_delay(&st, LMX1204_CH0,      1/*SYSREFOUT0_DELAY_PHASE_QCLK0*/, 7, 120);
    lmx1204_init_sysrefout_ch_delay(&st, LMX1204_CH1,      1/*SYSREFOUT0_DELAY_PHASE_QCLK0*/, 7, 120);
    lmx1204_init_sysrefout_ch_delay(&st, LMX1204_CH2,      1/*SYSREFOUT0_DELAY_PHASE_QCLK0*/, 7, 120);
    lmx1204_init_sysrefout_ch_delay(&st, LMX1204_CH3,      1/*SYSREFOUT0_DELAY_PHASE_QCLK0*/, 7, 120);
    lmx1204_init_sysrefout_ch_delay(&st, LMX1204_CH_LOGIC, 1/*SYSREFOUT0_DELAY_PHASE_QCLK0*/, 7, 120);
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
END_TEST

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
END_TEST

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
END_TEST

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
END_TEST

START_TEST(lmx1204_solver_test5)
{
    st.clkin = 500000000;
    st.clkout = st.clkin;

    st.sysrefreq = 0;
    st.sysrefout = 3125000;
    st.sysref_mode = LMX1204_CONTINUOUS;

    //st.ch_en[LMX1204_CH_LOGIC] = false;
    st.logiclkout = 125000000;

    int res = lmx1204_solver(&st, false, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

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
    tcase_add_test(tc_core, lmx1204_solver_test5);

    suite_add_tcase(s, tc_core);
    return s;
}

