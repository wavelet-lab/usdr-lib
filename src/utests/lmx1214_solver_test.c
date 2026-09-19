#include <stdio.h>
#include <check.h>
#include "lmx1214/lmx1214.h"

static lmx1214_state_t st;

static void setup()
{
    memset(&st, 0, sizeof(st));
}

static void teardown() {}

START_TEST(lmx1214_solver_test1)
{
    const uint64_t osc_in = 600000000;
    uint64_t out_freq = osc_in / 4;
    bool en[4] = {1,1,1,1};

    lmx1214_auxclkout_cfg_t aux;
    aux.enable = 1;
    aux.fmt = LMX1214_FMT_LVDS;
    aux.freq = osc_in / 16;

    int res = lmx1214_solver(&st, osc_in, out_freq, en, &aux, true, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx1214_solver_test2)
{
    const uint64_t osc_in = 640000000;
    uint64_t out_freq = osc_in;
    bool en[4] = {1,1,1,1};

    lmx1214_auxclkout_cfg_t aux;
    aux.enable = 1;
    aux.fmt = LMX1214_FMT_LVDS;
    aux.freq = osc_in / 160;

    int res = lmx1214_solver(&st, osc_in, out_freq, en, &aux, true, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx1214_solver_test3)
{
    const uint64_t osc_in = 640000000;
    uint64_t out_freq = osc_in;
    bool en[4] = {1,1,1,1};

    lmx1214_auxclkout_cfg_t aux;
    aux.enable = 1;
    aux.fmt = LMX1214_FMT_LVDS;
    aux.freq = osc_in;

    int res = lmx1214_solver(&st, osc_in, out_freq, en, &aux, true, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx1214_solver_test4_pesync0)
{
    const uint64_t osc_in = 1600000000;
    uint64_t out_freq = osc_in / 2;
    bool en[4] = {1,1,1,1};

    lmx1214_auxclkout_cfg_t aux;
    aux.enable = 1;
    aux.fmt = LMX1214_FMT_LVDS;
    aux.freq = 800000000/4;

    int res = lmx1214_solver(&st, osc_in, out_freq, en, &aux, false, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx1214_solver_test4_pesync1)
{
    const uint64_t osc_in = 1600000000;
    uint64_t out_freq = osc_in / 2;
    bool en[4] = {1,1,1,1};

    lmx1214_auxclkout_cfg_t aux;
    aux.enable = 0;
    aux.fmt = LMX1214_FMT_LVDS;
    aux.freq = 0;

    int res = lmx1214_solver(&st, osc_in, out_freq, en, &aux, true, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx1214_solver_test4_pesync2)
{
    const uint64_t osc_in = 1600000000;
    uint64_t out_freq = osc_in / 3;
    bool en[4] = {1,1,1,1};

    lmx1214_auxclkout_cfg_t aux;
    aux.enable = 1;
    aux.fmt = LMX1214_FMT_LVDS;
    aux.freq = 1600000000 / 124;

    int res = lmx1214_solver(&st, osc_in, out_freq, en, &aux, false, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmx1214_solver_test4_pesync3)
{
    const uint64_t osc_in = 550000000;
    uint64_t out_freq = osc_in;
    bool en[4] = {1,1,1,1};

    lmx1214_auxclkout_cfg_t aux;
    aux.enable = 1;
    aux.fmt = LMX1214_FMT_LVDS;
    aux.freq = osc_in;

    int res = lmx1214_solver(&st, osc_in, out_freq, en, &aux, false, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

Suite * lmx1214_solver_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("lmx1214_solver");
    tc_core = tcase_create("HW");
    tcase_set_timeout(tc_core, 1);
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, lmx1214_solver_test1);
    tcase_add_test(tc_core, lmx1214_solver_test2);
    tcase_add_test(tc_core, lmx1214_solver_test3);
    tcase_add_test(tc_core, lmx1214_solver_test4_pesync0);
    tcase_add_test(tc_core, lmx1214_solver_test4_pesync1);
    tcase_add_test(tc_core, lmx1214_solver_test4_pesync2);
    tcase_add_test(tc_core, lmx1214_solver_test4_pesync3);

    suite_add_tcase(s, tc_core);
    return s;
}

