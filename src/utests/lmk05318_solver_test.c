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
    lmk05318_out_config_t* p = &cfg[0];
    res = res ? res : lmk05318_port_request(p++, 0, 100000000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 1, 100000000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 2, 122880000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 3, 122880000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 4,  31250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5,   3840000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 6, 491520000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 7,         1,  true, OUT_OFF);
    ck_assert_int_eq( res, 0 );

    lmk05318_registers_map_reset();
}

static void teardown()
{
}

START_TEST(lmk05318_solver_test1)
{
    lmk05318_registers_map_reset();
    int res = lmk05318_solver(&dev, cfg, SIZEOF_ARRAY(cfg));
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmk05318_solver_test3)
{
    uint64_t fvco1 = 2500000000ull;
    uint64_t f0_3 = fvco1 / 16;
    uint64_t f4_7 = 12500000; //3840000;

    int res = 0;
    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 1, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 2, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 3, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 4, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 6, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 7, f4_7, false, OUT_OFF);
    ck_assert_int_eq( res, 0 );

    p = &cfg[0];
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL2);
    ck_assert_int_eq( res, 0 );

    lmk05318_registers_map_reset();
    res = lmk05318_solver(&dev, cfg, SIZEOF_ARRAY(cfg));
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmk05318_solver_test4)
{
    uint64_t fvco1 = 2500000000ull;
    uint64_t f0_3 = fvco1 / 16;
    uint64_t f4_7 = 12500000; //3840000;

    memset(cfg, 0, sizeof(cfg));

    int res = 0;
    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 1, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 2, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 3, f0_3, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 4, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 6, f4_7, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 7, f4_7, false, OUT_OFF);
    ck_assert_int_eq( res, 0 );

    p = &cfg[0];
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(p++, AFF_APLL2);
    ck_assert_int_eq( res, 0 );

    lmk05318_registers_map_reset();
    res = lmk05318_solver(&dev, cfg, 4);
    ck_assert_int_eq( res, 0 );

    lmk05318_registers_map_reset();
    res = lmk05318_solver(&dev, cfg + 4, 4);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmk05318_solver_test5)
{
    int res = 0;
    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 1, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 2, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 3, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 4, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 6,  10000000, false, LVCMOS_P_N);
    res = res ? res : lmk05318_port_request(p++, 7,         1, false, LVCMOS_P_N);
    ck_assert_int_eq( res, 0 );

    lmk05318_registers_map_reset();
    res = lmk05318_solver(&dev, cfg, SIZEOF_ARRAY(cfg));
    ck_assert_int_eq( res, 0 );

}
END_TEST

START_TEST(lmk05318_solver_pesync)
{
    int res = 0;
    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 1, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 2, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 3, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 4, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 6,  10000000, false, LVCMOS_P_N);
    res = res ? res : lmk05318_port_request(p++, 7,         1, false, LVCMOS_P_N);
    ck_assert_int_eq( res, 0 );

    lmk05318_dpll_settings_t dpll;
    memset(&dpll, 0, sizeof(dpll));
    dpll.enabled = true;
    dpll.en[LMK05318_PRIREF] = true;
    dpll.fref[LMK05318_PRIREF] = 1;
    dpll.type[LMK05318_PRIREF] = DPLL_REF_TYPE_DIFF_NOTERM;
    dpll.dc_mode[LMK05318_PRIREF] = DPLL_REF_DC_COUPLED_INT;
    dpll.buf_mode[LMK05318_PRIREF] = DPLL_REF_AC_BUF_HYST50_DC_EN;

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 12800000, XO_CMOS, false, &dpll, cfg, SIZEOF_ARRAY(cfg), &st, true /*dry_run*/);
    ck_assert_int_eq( res, 0 );

}
END_TEST

START_TEST(lmk05318_solver_pesync_free_run)
{
    int res = 0;
    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 1, 125000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 2, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 3, 250000000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 4, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, 156250000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 6,  10000000, false, LVCMOS_P_N);
    res = res ? res : lmk05318_port_request(p++, 7,         1, false, LVCMOS_P_N);
    ck_assert_int_eq( res, 0 );

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 25000000, XO_CMOS, false, NULL, cfg, SIZEOF_ARRAY(cfg), &st, true /*dry_run*/);
    ck_assert_int_eq( res, 0 );
}
END_TEST

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
END_TEST

START_TEST(lmk05318_dsdr_test1)
{
    int res = 0;

    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, 491520000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 1, 491520000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 2, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 3, 3840000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 4, 0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, 122880000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 6, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 7, 122880000, false, LVDS);
    ck_assert_int_eq( res, 0 );

    lmk05318_dpll_settings_t dpll;
    memset(&dpll, 0, sizeof(dpll));

    dpll.enabled = true;
    dpll.en[LMK05318_PRIREF] = true;
    dpll.fref[LMK05318_PRIREF] = 40000000;
    dpll.type[LMK05318_PRIREF] = DPLL_REF_TYPE_DIFF_NOTERM;
    dpll.dc_mode[LMK05318_PRIREF] = DPLL_REF_DC_COUPLED_INT;
    dpll.buf_mode[LMK05318_PRIREF] = DPLL_REF_AC_BUF_HYST50_DC_EN;

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 26000000, XO_CMOS, false, &dpll, cfg, SIZEOF_ARRAY(cfg), &st, true /*dry_run*/);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmk05318_dsdr_test2)
{
    int res = 0;

    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, 491520000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 1, 491520000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 2, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 3, 3840000, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 4, 0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, 122880000 * 2, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 6, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 7, 122880000 * 2, false, LVDS);
    ck_assert_int_eq( res, 0 );

    lmk05318_dpll_settings_t dpll;
    memset(&dpll, 0, sizeof(dpll));

    dpll.enabled = true;
    dpll.en[LMK05318_PRIREF] = true;
    dpll.fref[LMK05318_PRIREF] = 40000000;
    dpll.type[LMK05318_PRIREF] = DPLL_REF_TYPE_DIFF_NOTERM;
    dpll.dc_mode[LMK05318_PRIREF] = DPLL_REF_DC_COUPLED_INT;
    dpll.buf_mode[LMK05318_PRIREF] = DPLL_REF_AC_BUF_HYST50_DC_EN;

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 26000000, XO_CMOS, false, &dpll, cfg, SIZEOF_ARRAY(cfg), &st, true /*dry_run*/);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmk05318_dsdr_test3)
{
    int res = 0;

    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, 491520000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 1, 491520000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 2, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 3, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 4, 0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, 245760000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 6, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 7, 245760000, false, LVDS);
    ck_assert_int_eq( res, 0 );

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 26000000, XO_CMOS, false, NULL, cfg, SIZEOF_ARRAY(cfg), &st, true /*dry_run*/);
    ck_assert_int_eq( res, 0 );
}
END_TEST


static int simplesync_pd_low_chs(lmk05318_state_t* st)
{
    int res = 0;
    res = res ? res : lmk05318_set_out_mux(st, 0, 0, OUT_OFF);
    res = res ? res : lmk05318_set_out_mux(st, 1, 0, OUT_OFF);
    res = res ? res : lmk05318_set_out_mux(st, 2, 0, OUT_OFF);
    res = res ? res : lmk05318_set_out_mux(st, 3, 0, OUT_OFF);
    res = res ? res : lmk05318_reg_wr_from_map(st, true /*dry_run*/);
    return res;
}

#define LO_FREQ_CUTOFF  3500000ul // VCO2_MIN / 7 / 256 = 3069196.43 Hz

static int lmk05318_simplesync_set_lo_freq(lmk05318_state_t* st, uint64_t meas_lo)
{
    int res;
    if(meas_lo < LO_FREQ_CUTOFF)
    {
        res = simplesync_pd_low_chs(st);
    }
    else
    {
        lmk05318_out_config_t cfg2[4];

        lmk05318_port_request(&cfg2[0], 0, meas_lo, false, LVDS);
        lmk05318_port_request(&cfg2[1], 1, meas_lo, false, LVDS);
        lmk05318_port_request(&cfg2[2], 2, meas_lo, false, LVDS);
        lmk05318_port_request(&cfg2[3], 3, meas_lo, false, LVDS);

        lmk05318_set_port_affinity(&cfg2[0], AFF_APLL2);
        lmk05318_set_port_affinity(&cfg2[1], AFF_APLL2);
        lmk05318_set_port_affinity(&cfg2[2], AFF_APLL2);
        lmk05318_set_port_affinity(&cfg2[3], AFF_APLL2);

        res = lmk05318_solver(st, cfg2, SIZEOF_ARRAY(cfg2));
        res = res ? res : lmk05318_reg_wr_from_map(st, true /*dry_run*/);
    }

    return res;
}

START_TEST(lmk05318_simplesync_test1)
{
    int res = 0;

    lmk05318_out_config_t cfg1[4];
    lmk05318_out_config_t* p = &cfg1[0];

    lmk05318_port_request(p++, 4, 25000000, false, LVCMOS_P_N);
    lmk05318_port_request(p++, 5, 25000000, false, LVCMOS_P_N);
    lmk05318_port_request(p++, 6, 25000000, false, LVCMOS_P_N);
    lmk05318_port_request(p++, 7, 25000000, false, LVCMOS_P_N);

    p = &cfg1[0];

    lmk05318_set_port_affinity(p++, AFF_APLL1);
    lmk05318_set_port_affinity(p++, AFF_APLL1);
    lmk05318_set_port_affinity(p++, AFF_APLL1);
    lmk05318_set_port_affinity(p++, AFF_APLL1);

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 26000000, XO_CMOS, false, NULL, cfg1, SIZEOF_ARRAY(cfg1), &st, true /*dry_run*/);
    res = res ? res : simplesync_pd_low_chs(&st);

    ck_assert_int_eq( res, 0 );

    res = lmk05318_simplesync_set_lo_freq(&st, 122800000);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_simplesync_set_lo_freq(&st, 3500000);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_simplesync_set_lo_freq(&st, 3000000);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_simplesync_set_lo_freq(&st, 0);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmk05318_solver_test_xmass)
{
    int res = 0;

    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0,          0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 1,          0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 2,          0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 3,          0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 4, 1000666000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 5,          0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 6,   25000000, false, LVCMOS_P_N);
    res = res ? res : lmk05318_port_request(p++, 7,          1, false, LVCMOS_P_N);

    res = res ? res : lmk05318_set_port_affinity(&cfg[4], AFF_APLL2);
    res = res ? res : lmk05318_set_port_affinity(&cfg[6], AFF_APLL1);
    res = res ? res : lmk05318_set_port_affinity(&cfg[7], AFF_APLL1);
    ck_assert_int_eq( res, 0 );

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 26000000, XO_CMOS, false, NULL, cfg, SIZEOF_ARRAY(cfg), &st, true /*dry_run*/);

    ck_assert_int_eq( res, 0 );

    res = lmk05318_port_request(&cfg[4], 4, 4000000, false, LVDS);
    res = res ? res : lmk05318_set_port_affinity(&cfg[4], AFF_APLL2);
    res = res ? res : lmk05318_solver(&st, cfg, 8);
    res = res ? res : lmk05318_reg_wr_from_map(&st, true);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_port_request(&cfg[4], 4, 5000000, false, LVDS);
    res = res ? res : lmk05318_set_port_affinity(&cfg[4], AFF_APLL2);
    res = res ? res : lmk05318_solver(&st, &cfg[4], 1);
    res = res ? res : lmk05318_reg_wr_from_map(&st, true);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_port_request(&cfg[4], 4, 0, false, LVDS);
    res = res ? res : lmk05318_set_port_affinity(&cfg[4], AFF_APLL2);
    res = res ? res : lmk05318_solver(&st, cfg, 8);
    res = res ? res : lmk05318_reg_wr_from_map(&st, true);
    ck_assert_int_eq( res, 0 );

    res = lmk05318_port_request(&cfg[4], 4, 6000000, false, LVDS);
    res = res ? res : lmk05318_set_port_affinity(&cfg[4], AFF_APLL2);
    res = res ? res : lmk05318_solver(&st, cfg, 8);
    res = res ? res : lmk05318_reg_wr_from_map(&st, true);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmk05318_hyper_test1)
{
    int res = 0;

    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, 491520000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 1, 491520000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 2, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 3, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 4, 0, false, OUT_OFF);
    res = res ? res : lmk05318_port_request(p++, 5, 245760000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 6, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 7, 245760000, false, LVDS);
    ck_assert_int_eq( res, 0 );

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 52000000, XO_CMOS, false, NULL, cfg, SIZEOF_ARRAY(cfg), &st, true /*dry_run*/);
    ck_assert_int_eq( res, 0 );
}
END_TEST

START_TEST(lmk05318_customer_test1)
{
    int res = 0;

    lmk05318_out_config_t* p = &cfg[0];

    res = res ? res : lmk05318_port_request(p++, 0, 491520000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 1, 491520000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 2, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 3, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 4, 312500000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 5, 245760000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 6, 3840000, false, LVDS);
    res = res ? res : lmk05318_port_request(p++, 7, 245760000, false, LVDS);
    ck_assert_int_eq( res, 0 );

    lmk05318_state_t st;
    res = lmk05318_create(NULL, 0, 0, 52000000, XO_CMOS, false, NULL, cfg, SIZEOF_ARRAY(cfg), &st, true /*dry_run*/);
    ck_assert_int_eq( res, 0 );
}
END_TEST


Suite * lmk05318_solver_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("lmk05318_solver");
    tc_core = tcase_create("HW");
    tcase_set_timeout(tc_core, 1);
    tcase_add_checked_fixture(tc_core, setup, teardown);
/*
    tcase_add_test(tc_core, lmk05318_solver_test1);
    tcase_add_test(tc_core, lmk05318_solver_test3);
    tcase_add_test(tc_core, lmk05318_solver_test4);
    tcase_add_test(tc_core, lmk05318_solver_test5);
    tcase_add_test(tc_core, lmk05318_solver_pesync);
    tcase_add_test(tc_core, lmk05318_solver_pesync_free_run);
    tcase_add_test(tc_core, lmk05318_dpll_test1);
    tcase_add_test(tc_core, lmk05318_dsdr_test1);
    tcase_add_test(tc_core, lmk05318_dsdr_test2);
    tcase_add_test(tc_core, lmk05318_dsdr_test3);
    tcase_add_test(tc_core, lmk05318_simplesync_test1);
    tcase_add_test(tc_core, lmk05318_solver_test_xmass);
    tcase_add_test(tc_core, lmk05318_hyper_test1); */

    tcase_add_test(tc_core, lmk05318_customer_test1);

    suite_add_tcase(s, tc_core);
    return s;
}
