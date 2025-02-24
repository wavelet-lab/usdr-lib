#include <check.h>
#include "lmk05318/lmk05318.h"

#define OUTS_LEN LMK05318_MAX_OUT_PORTS
#define DELTA_PLUS 2
#define DELTA_MINUS 2

static lmk05318_out_config_t cfg[OUTS_LEN];

static void setup()
{
    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, 100000000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 1, 100000000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 2, 122880000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 3, 122880000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 4,  31250000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5,   3840000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6, 491520000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 7,         1, DELTA_PLUS, DELTA_MINUS,  true, OUT_OFF);
    ck_assert_int_eq( res, 0 );
}

static void teardown()
{
}

START_TEST(lmk05318_solver_test1)
{
    int res = lmk05318_solver(NULL, cfg, OUTS_LEN, true);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmk05318_solver_test2)
{
    int res = 0;
    const uint64_t f = 5659995555ull;

    res = res ? res : lmk05318_port_request(cfg, 2, f/7/256, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 3, f/7/256, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5,   f/2/4, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6,  f/7/17, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_solver(NULL, cfg, OUTS_LEN, true);
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
