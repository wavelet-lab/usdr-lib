#include <check.h>
#include "lmk05318/lmk05318.h"

#define OUTS_LEN 8
#define DELTA_PLUS 2
#define DELTA_MINUS 2

static lmk05318_out_config_t cfg[OUTS_LEN];

static void setup()
{
    memset(cfg, 0, sizeof(cfg));

    cfg[0].port = 0;
    cfg[0].wanted.freq = 100000000;
    cfg[0].wanted.freq_delta_minus = DELTA_MINUS;
    cfg[0].wanted.freq_delta_plus = DELTA_PLUS;
    cfg[0].wanted.revert_phase = false;
    cfg[0].wanted.type = OUT_OFF;

    cfg[1].port = 1;
    cfg[1].wanted.freq = 100000000;
    cfg[1].wanted.freq_delta_minus = DELTA_MINUS;
    cfg[1].wanted.freq_delta_plus = DELTA_PLUS;
    cfg[1].wanted.revert_phase = false;
    cfg[1].wanted.type = OUT_OFF;

    cfg[2].port = 2;
    cfg[2].wanted.freq = 122880000;
    cfg[2].wanted.freq_delta_minus = DELTA_MINUS;
    cfg[2].wanted.freq_delta_plus = DELTA_PLUS;
    cfg[2].wanted.revert_phase = false;
    cfg[2].wanted.type = OUT_OFF;

    cfg[3].port = 3;
    cfg[3].wanted.freq = 122880000;
    cfg[3].wanted.freq_delta_minus = DELTA_MINUS;
    cfg[3].wanted.freq_delta_plus = DELTA_PLUS;
    cfg[3].wanted.revert_phase = false;
    cfg[3].wanted.type = OUT_OFF;

    cfg[4].port = 4;
    cfg[4].wanted.freq = 31250000;
    cfg[4].wanted.freq_delta_minus = DELTA_MINUS;
    cfg[4].wanted.freq_delta_plus = DELTA_PLUS;
    cfg[4].wanted.revert_phase = false;
    cfg[4].wanted.type = OUT_OFF;

    cfg[5].port = 5;
    cfg[5].wanted.freq = 3840000;
    cfg[5].wanted.freq_delta_minus = DELTA_MINUS;
    cfg[5].wanted.freq_delta_plus = DELTA_PLUS;
    cfg[5].wanted.revert_phase = false;
    cfg[5].wanted.type = OUT_OFF;

    cfg[6].port = 6;
    cfg[6].wanted.freq = 491520000;
    cfg[6].wanted.freq_delta_minus = DELTA_MINUS;
    cfg[6].wanted.freq_delta_plus = DELTA_PLUS;
    cfg[6].wanted.revert_phase = false;
    cfg[6].wanted.type = OUT_OFF;

    cfg[7].port = 7;
    cfg[7].wanted.freq = 1;
    cfg[7].wanted.freq_delta_minus = DELTA_MINUS;
    cfg[7].wanted.freq_delta_plus = DELTA_PLUS;
    cfg[7].wanted.revert_phase = true;
    cfg[7].wanted.type = OUT_OFF;
}

static void teardown()
{
}

START_TEST(lmk05318_solver_test1)
{
    int res = lmk05318_solver(NULL, cfg, 8);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmk05318_solver_test2)
{
    const uint64_t f = 5659995555ull;
    cfg[2].wanted.freq = cfg[3].wanted.freq =   f/7/256;
    cfg[5].wanted.freq =                        f/2/4;
    cfg[6].wanted.freq =                        f/7/17;

    int res = lmk05318_solver(NULL, cfg, 8);
    ck_assert_int_eq( res, 0 );
}

Suite * lmk05318_solver_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("lmk05318_solver");
    tc_core = tcase_create("HW");
    tcase_set_timeout(tc_core, 1);
    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, lmk05318_solver_test1);
    tcase_add_test(tc_core, lmk05318_solver_test2);
    suite_add_tcase(s, tc_core);
    return s;
}
