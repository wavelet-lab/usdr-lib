#include <check.h>
#include "lmk05318/lmk05318.h"

#define OUTS_LEN LMK05318_MAX_OUT_PORTS

static lmk05318_out_config_t cfg[OUTS_LEN];
static lmk05318_state_t dev;

void lmk05318_registers_map_reset();

static void setup()
{
    memset(cfg, 0, sizeof(cfg));
    memset(&dev, 0, sizeof(dev));

    dev.fref_pll2_div_rp = 3;
    dev.fref_pll2_div_rs = 6;

    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, 100000000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 1, 100000000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 2, 122880000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 3, 122880000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 4,  31250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5,   3840000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6, 491520000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 7,         1,  true, OUT_OFF);
    ck_assert_int_eq( res, 0 );

    lmk05318_registers_map_reset();
}

static void teardown()
{
}

START_TEST(lmk05318_solver_test1)
{
    lmk05318_registers_map_reset();
    int res = lmk05318_solver(&dev, cfg, OUTS_LEN);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmk05318_solver_test3)
{
    uint64_t fvco1 = 2500000000ull;
    uint64_t f0_3 = fvco1 / 16;
    uint64_t f4_7 = 12500000; //3840000;

    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 1, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 2, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 3, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 4, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 7, f4_7, false, OUT_OFF);
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

    lmk05318_registers_map_reset();
    res = lmk05318_solver(&dev, cfg, OUTS_LEN);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmk05318_solver_test4)
{
    uint64_t fvco1 = 2500000000ull;
    uint64_t f0_3 = fvco1 / 16;
    uint64_t f4_7 = 12500000; //3840000;

    memset(cfg, 0, sizeof(cfg));

    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 1, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 3, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 4, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 7, f4_7, false, OUT_OFF);
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

    lmk05318_registers_map_reset();
    res = lmk05318_solver(&dev, cfg, 4);
    ck_assert_int_eq( res, 0 );

    lmk05318_registers_map_reset();
    res = lmk05318_solver(&dev, cfg + 4, 4);
    ck_assert_int_eq( res, 0 );
}

START_TEST(lmk05318_solver_test5)
{
    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 1, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 2, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 3, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 4, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6,  10000000, false, LVCMOS);
    res = res ? res : lmk05318_port_request(cfg, 7,         1, false, LVCMOS);
    ck_assert_int_eq( res, 0 );

    lmk05318_registers_map_reset();
    res = lmk05318_solver(&dev, cfg, OUTS_LEN);
    ck_assert_int_eq( res, 0 );

}

START_TEST(lmk05318_solver_test6)
{
    int res = 0;
    res = res ? res : lmk05318_port_request(cfg, 0, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 1, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 2, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 3, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(cfg, 4, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 5, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(cfg, 6,  10000000, false, LVCMOS);
    res = res ? res : lmk05318_port_request(cfg, 7,         1, false, LVCMOS);
    ck_assert_int_eq( res, 0 );

    lmk05318_xo_settings_t xo;
    xo.fref = 25000000;
    xo.doubler_enabled = true;
    xo.fdet_bypass = false;
    xo.pll1_fref_rdiv = 1;
    xo.type = XO_CMOS;

    lmk05318_dpll_settings_t dpll;
    memset(&dpll, 0, sizeof(dpll));
    dpll.enabled = true;
    dpll.en[LMK05318_PRIREF] = true;
    dpll.fref[LMK05318_PRIREF] = 1;
    dpll.type[LMK05318_PRIREF] = DPLL_REF_TYPE_DIFF_NOTERM;
    dpll.dc_mode[LMK05318_PRIREF] = DPLL_REF_DC_COUPLED_INT;
    dpll.buf_mode[LMK05318_PRIREF] = DPLL_REF_AC_BUF_HYST50_DC_EN;

    lmk05318_state_t st;
    memset(&st, 0, sizeof(st));

    res = lmk05318_create(NULL, 0, 0, &xo, &dpll, cfg, 8, &st, true /*dry_run*/);
    ck_assert_int_eq( res, 0 );

}

START_TEST(lmk05318_dpll_test1)
{
    lmk05318_dpll_settings_t dpll;
    memset(&dpll, 0, sizeof(dpll));

    dpll.enabled = true;
    dpll.en[LMK05318_PRIREF] = true;
    dpll.fref[LMK05318_PRIREF] = 1;
    dpll.type[LMK05318_PRIREF] = DPLL_REF_TYPE_DIFF_NOTERM;
    dpll.dc_mode[LMK05318_PRIREF] = DPLL_REF_DC_COUPLED_INT;
    dpll.buf_mode[LMK05318_PRIREF] = DPLL_REF_AC_BUF_HYST50_DC_EN;

    int res = lmk05318_dpll_config(&dev, &dpll);
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
    tcase_add_test(tc_core, lmk05318_solver_test3);
    tcase_add_test(tc_core, lmk05318_solver_test4);
    tcase_add_test(tc_core, lmk05318_solver_test5);
    tcase_add_test(tc_core, lmk05318_solver_test6);
    tcase_add_test(tc_core, lmk05318_dpll_test1);

    suite_add_tcase(s, tc_core);
    return s;
}
