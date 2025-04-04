#include <check.h>
#include "lmk05318/lmk05318.h"

#define OUTS_LEN LMK05318_MAX_OUT_PORTS
#define DELTA_PLUS 2
#define DELTA_MINUS 2

static lmk05318_out_config_t cfg[OUTS_LEN];

static void setup()
{
    memset(cfg, 0, sizeof(cfg));

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


START_TEST(lmk05318_solver_test3)
{
    uint64_t fvco1 = 2500000000ull;
    uint64_t f0_3 = fvco1 / 123;
    uint64_t f4_7 = 12500000; //3840000;

    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, f0_3, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 1, f0_3, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 2, f0_3, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 3, f0_3, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 4, f4_7, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5, f4_7, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6, f4_7, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 7, f4_7, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    ck_assert_int_eq( res, 0 );

    res = res ? res : lmk05318_set_port_affinity(cfg, 0, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(cfg, 1, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(cfg, 2, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(cfg, 3, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(cfg, 4, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(cfg, 5, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(cfg, 6, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(cfg, 7, AFF_APLL2);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_solver(NULL, cfg, OUTS_LEN, true);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmk05318_solver_test4)
{
    uint64_t fvco1 = 2500000000ull;
    uint64_t f0_3 = fvco1 / 123;
    uint64_t f4_7 = 12500000; //3840000;

    memset(cfg, 0, sizeof(cfg));

    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, f0_3, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 1, f0_3, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    //res = res ? res : lmk05318_port_request(cfg, 2, f0_3, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 3, f0_3, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 4, f4_7, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5, f4_7, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    //res = res ? res : lmk05318_port_request(cfg, 6, f4_7, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 7, f4_7, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    ck_assert_int_eq( res, 0 );

    res = res ? res : lmk05318_set_port_affinity(cfg, 0, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(cfg, 1, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(cfg, 2, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(cfg, 3, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(cfg, 4, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(cfg, 5, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(cfg, 6, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(cfg, 7, AFF_APLL2);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_solver(NULL, cfg, 4, true);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_solver(NULL, cfg + 4, 4, true);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmk05318_solver_test5)
{
/*  // TODO: Initialize LMK05318B
    // XO: 25Mhz
    //
    // OUT0: LVDS       125.000 Mhz
    // OUT1: LVDS       125.000 Mhz
    // OUT2: LVDS       250.000 Mhz MASH_ORD 0/1/2 | 156.250 Mhz MASH_ORD 3
    // OUT3: LVDS       250.000 Mhz MASH_ORD 0/1/2 | 156.250 Mhz MASH_ORD 3
    // OUT4: LVDS OFF   156.250 Mhz  | OFF by default
    // OUT5: LVDS OFF   156.250 Mhz  | OFF by default
    // OUT6: Dual CMOS   10.000 Mhz
    // OUT7: Dual CMOS        1 Hz
    // res = res ? res : lmk05318_create()
 */

    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, 125000000, DELTA_PLUS, DELTA_MINUS, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 1, 125000000, DELTA_PLUS, DELTA_MINUS, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 2, 250000000, DELTA_PLUS, DELTA_MINUS, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 3, 250000000, DELTA_PLUS, DELTA_MINUS, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 4, 156250000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5, 156250000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6,  10000000, DELTA_PLUS, DELTA_MINUS, false, LVCMOS);
    res = res ? res : lmk05318_port_request(cfg, 7,         1, DELTA_PLUS, DELTA_MINUS, false, LVCMOS);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_solver(NULL, cfg, OUTS_LEN, true);
    ck_assert_int_eq( res, 0 );

}

START_TEST(lmk05318_solver_test6)
{
    /*  // TODO: Initialize LMK05318B
    // XO: 25Mhz
    //
    // OUT0: LVDS       125.000 Mhz
    // OUT1: LVDS       125.000 Mhz
    // OUT2: LVDS       250.000 Mhz MASH_ORD 0/1/2 | 156.250 Mhz MASH_ORD 3
    // OUT3: LVDS       250.000 Mhz MASH_ORD 0/1/2 | 156.250 Mhz MASH_ORD 3
    // OUT4: LVDS OFF   156.250 Mhz  | OFF by default
    // OUT5: LVDS OFF   156.250 Mhz  | OFF by default
    // OUT6: Dual CMOS   10.000 Mhz
    // OUT7: Dual CMOS        1 Hz
    // res = res ? res : lmk05318_create()
 */

    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, 125000000, DELTA_PLUS, DELTA_MINUS, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 1, 125000000, DELTA_PLUS, DELTA_MINUS, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 2, 250000000, DELTA_PLUS, DELTA_MINUS, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 3, 250000000, DELTA_PLUS, DELTA_MINUS, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 4, 156250000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5,  13000000, DELTA_PLUS, DELTA_MINUS, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6,  13000000, DELTA_PLUS, DELTA_MINUS, false, LVCMOS);
    res = res ? res : lmk05318_port_request(cfg, 7,         1, DELTA_PLUS, DELTA_MINUS, false, LVCMOS);
    ck_assert_int_eq( res, 0 );

    lmk05318_xo_settings_t xo;
    xo.fref = 25000000;
    xo.doubler_enabled = true;
    xo.fdet_bypass = false;
    xo.pll1_fref_rdiv = 1;
    xo.type = XO_CMOS;

    lmk05318_state_t st;
    memset(&st, 0, sizeof(st));

    res = lmk05318_create_ex(NULL, 0, 0, &xo, false, cfg, 8, &st, true /*dry_run*/);
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

    //tcase_add_test(tc_core, lmk05318_solver_test1);
    //tcase_add_test(tc_core, lmk05318_solver_test2);
    //tcase_add_test(tc_core, lmk05318_solver_test3);
    //tcase_add_test(tc_core, lmk05318_solver_test4);
    //tcase_add_test(tc_core, lmk05318_solver_test5);
    tcase_add_test(tc_core, lmk05318_solver_test6);

    suite_add_tcase(s, tc_core);
    return s;
}
