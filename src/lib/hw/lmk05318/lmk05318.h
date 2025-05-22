// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMK05318_H
#define LMK05318_H

#include <usdr_lowlevel.h>

#define LMK05318_MAX_OUT_PORTS 8
#define LMK05318_MAX_REAL_PORTS (LMK05318_MAX_OUT_PORTS - 2)

enum xo_input_type
{
    XO_DC_DIFF_EXT = 0,
    XO_AC_DIFF_EXT,
    XO_AC_DIFF_INT_100,
    XO_HCSL_INT_50,
    XO_CMOS,
    XO_SE_INT_50,
};
typedef enum xo_input_type xo_input_type_t;

enum
{
    DPLL_REF_TYPE_DIFF_NOTERM = 1,
    DPLL_REF_TYPE_DIFF_100 = 3,
    DPLL_REF_TYPE_DIFF_50 = 5,
    DPLL_REF_TYPE_SE_NOTERM = 8,
    DPLL_REF_TYPE_SE_50 = 0xC,
};

enum
{
    DPLL_REF_AC_COUPLED_INT = 0,
    DPLL_REF_DC_COUPLED_INT = 1,
};

enum
{
    DPLL_REF_AC_BUF_HYST50_DC_EN = 0,
    DPLL_REF_AC_BUF_HYST200_DC_DIS = 1,
};

struct lmk05318_xo_settings
{
    unsigned pll1_fref_rdiv;
    uint32_t fref;
    xo_input_type_t type;
    bool doubler_enabled;
    bool fdet_bypass;
};
typedef struct lmk05318_xo_settings lmk05318_xo_settings_t;

enum
{
    LMK05318_PRIREF = 0,
    LMK05318_SECREF = 1,
};

struct lmk05318_dpll_settings
{
    bool enabled;
    bool en[2];
    uint64_t fref[2];
    uint8_t dc_mode[2];
    uint8_t buf_mode[2];
    uint8_t type[2];
};
typedef struct lmk05318_dpll_settings lmk05318_dpll_settings_t;

struct lmk05318_output
{
    double freq;
    uint64_t odiv;
    int mux;
};
typedef struct lmk05318_output lmk05318_output_t;

struct lmk05318_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    // Ref deviders
    unsigned fref_pll2_div_rp; // 3 to 6
    unsigned fref_pll2_div_rs; // 1 - 32

    // VCO2 freq
    uint64_t vco2_freq;
    unsigned vco2_n, vco2_num, vco2_den;
    unsigned pd1, pd2;

    lmk05318_output_t outputs[LMK05318_MAX_OUT_PORTS];

    struct {
        bool enabled;
        bool ref_en[2];
        uint16_t rdiv[2];
        double ftdc;
        double lbw;
        uint8_t pre_div;
        uint64_t n, num, den;
        bool zdm;
    } dpll;

    lmk05318_xo_settings_t xo;
};

enum lmk05318_type {
    OUT_OFF = 0,
    //
    LVDS,
    CML,
    LVPECL,
    HCSL_EXT_50,
    HCSL_INT_50,
    // formats below supported by ports 4..7 only
    LVCMOS_HIZ_HIZ,
    LVCMOS_HIZ_N,
    LVCMOS_HIZ_P,
    LVCMOS_LOW_LOW,
    LVCMOS_N_HIZ,
    LVCMOS_N_N,
    LVCMOS_N_P,
    LVCMOS_P_HIZ,
    LVCMOS_P_N,
    LVCMOS_P_P,
};

typedef struct lmk05318_state lmk05318_state_t;
typedef enum lmk05318_type lmk05318_type_t;

enum lmk05318_port_affinity
{
    AFF_ANY = 0,
    AFF_APLL1,
    AFF_APLL2
};
typedef enum lmk05318_port_affinity lmk05318_port_affinity_t;

struct lmk05318_out_config
{
    unsigned port; //0..7

    // these fields are inputs
    struct
    {
        uint32_t freq;
        unsigned freq_delta_plus, freq_delta_minus;
        bool revert_phase;
        lmk05318_type_t type;
        lmk05318_port_affinity_t pll_affinity;
    } wanted;

    // these fields are results
    struct
    {
        double freq;
        uint64_t out_div;
        int mux;
    } result;

    //*
    // these fields are for internal use, do not touch them. Use lmk05318_port_request().
    bool solved;
    uint64_t max_odiv;
    uint32_t freq_min, freq_max;
    uint32_t pd_min, pd_max;
    //*
};
typedef struct lmk05318_out_config lmk05318_out_config_t;

#define LMK05318_FREQ_DELTA 2

static inline int lmk05318_port_request(lmk05318_out_config_t* p,
                                        unsigned port,
                                        uint32_t freq,
                                        bool revert_phase,
                                        lmk05318_type_t type)
{
    if(port > LMK05318_MAX_OUT_PORTS - 1)
        return -EINVAL;

    memset(p, 0, sizeof(*p));
    p->port = port;
    p->wanted.freq = freq;
    p->wanted.freq_delta_plus = LMK05318_FREQ_DELTA;
    p->wanted.freq_delta_minus = LMK05318_FREQ_DELTA;
    p->wanted.revert_phase = revert_phase;
    p->wanted.type = type;
    p->wanted.pll_affinity = AFF_ANY;
    p->solved = false;
    return 0;
}

static inline int lmk05318_set_port_affinity(lmk05318_out_config_t* p, lmk05318_port_affinity_t aff)
{
    p->wanted.pll_affinity = aff;
    return 0;
}

enum lock_msk {
    LMK05318_LOS_XO = 1,
    LMK05318_LOL_PLL1 = 2,
    LMK05318_LOL_PLL2 = 4,
    LMK05318_LOS_FDET_XO = 8,

    LMK05318_LOPL_DPLL = 16,
    LMK05318_LOFL_DPLL = 32,
    LMK05318_BAW_LOCK = 64,
};

int lmk05318_set_out_div(lmk05318_state_t* d, unsigned port, uint64_t div);
int lmk05318_sync(lmk05318_state_t* out);
int lmk05318_mute(lmk05318_state_t* out, uint8_t chmask);
int lmk05318_reset_los_flags(lmk05318_state_t* d);
int lmk05318_check_lock(lmk05318_state_t* d, unsigned* los_msk, bool silent);
int lmk05318_wait_apll1_lock(lmk05318_state_t* d, unsigned timeout);
int lmk05318_wait_apll2_lock(lmk05318_state_t* d, unsigned timeout);
int lmk05318_softreset(lmk05318_state_t* out);
int lmk05318_set_out_mux(lmk05318_state_t* d, unsigned port, unsigned mux, unsigned otype);

int lmk05318_reg_wr(lmk05318_state_t* d, uint16_t reg, uint8_t out);
int lmk05318_reg_rd(lmk05318_state_t* d, uint16_t reg, uint8_t* val);
int lmk05318_reg_wr_from_map(lmk05318_state_t* d, bool dry_run);

int lmk05318_set_xo_fref(lmk05318_state_t* d);
int lmk05318_tune_apll1(lmk05318_state_t* d);

int lmk05318_solver(lmk05318_state_t* d, lmk05318_out_config_t* _outs, unsigned n_outs);

int lmk05318_create(lldev_t dev, unsigned subdev, unsigned lsaddr,
                    const lmk05318_xo_settings_t* xo, lmk05318_dpll_settings_t* dpll,
                    lmk05318_out_config_t* out_ports_cfg, unsigned out_ports_len,
                    lmk05318_state_t* out, bool dry_run);

int lmk05318_dpll_config(lmk05318_state_t* d, lmk05318_dpll_settings_t* dpll);
int lmk05318_wait_dpll_ref_stat(lmk05318_state_t* d, unsigned timeout);

#endif
