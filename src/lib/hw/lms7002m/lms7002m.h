// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMS7002M_H
#define LMS7002M_H

// LMS7002M control logic mostly for block specific perfective
#include <stdint.h>
#include <usdr_lowlevel.h>

// RFE path configuration for a single channel
struct lms7002m_rfe_cfg {
    uint16_t path : 2; // lms7002m_rfe_path_t
    uint16_t lb : 1; // Loopback activated
    uint16_t en : 1;

    uint16_t lna : 4; // LNA gain
    uint16_t lbg : 4; // Loopback gain
    uint16_t tia : 2; // Tia gain
};

struct lms7002m_trf_cfg {
    uint16_t lb : 1; // Loopback activated
    uint16_t en : 1;
    uint16_t path : 2; // lms7002m_trf_path_t

    uint16_t gain : 5;
    uint16_t lbloss : 3;
};

struct lms7002m_rbb_cfg {
    uint8_t lb : 1;      // Activate LB from TX
    uint8_t lpf_byp : 1; //Bypass LPF
    uint8_t lpf_lnh : 1; // 0 - HBF, 1 - LBF
    uint8_t ext_ad : 1;
    //uint8_t blocker : 1;
};

struct lms7002m_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    // Configurations
    struct lms7002m_rfe_cfg rfe[2];
    struct lms7002m_rbb_cfg rbb[2];
    struct lms7002m_trf_cfg trf[2];

    //Temp value for callbacks and inter proc calls
    uint16_t temp;

    // Cached values for
    uint16_t reg_mac;  // MAC state before API call
    uint16_t reg_amac; // Active MAC for proper logging

    uint16_t reg_en_dir[2]; //Enable control for SXX / RXX / RFE / TBB / TRF
    uint16_t reg_rxtsp_dscpcfg[2];
    uint16_t reg_rxtsp_dscmode[2];
    uint16_t reg_rxtsp_hbdo_iq[2];
    uint16_t reg_txtsp_dscpcfg[2];
    uint16_t reg_txtsp_dscmode[2];
    uint16_t reg_txtsp_hbdo_iq[2];
    int8_t   reg_tbb_gc_corr[2];
    uint8_t  reg_tbb_gc[2];
};
typedef struct lms7002m_state lms7002m_state_t;

struct lms7002m_lpf_params
{
    unsigned rcc;
    unsigned r;
    unsigned c;
};
typedef struct lms7002m_lpf_params lms7002m_lpf_params_t;


int lms7002m_create(lldev_t dev, unsigned subdev, unsigned lsaddr, uint32_t lms_ldo_mask, bool txrx_clk, lms7002m_state_t *out);
int lms7002m_destroy(lms7002m_state_t* m);

// Helpers
enum lms7002m_mac_mode {
    LMS7_CH_NONE = 0,
    LMS7_CH_A = 1,
    LMS7_CH_B = 2,
    LMS7_CH_AB = LMS7_CH_A | LMS7_CH_B,
};
typedef enum lms7002m_mac_mode lms7002m_mac_mode_t;
int lms7002m_mac_set(lms7002m_state_t* m, unsigned mac);

// LimeLight
enum lms7002m_lml_map_options {
    LML_AI = 0,
    LML_AQ = 1,
    LML_BI = 2,
    LML_BQ = 3,
};
struct lms7002m_lml_map {
    uint8_t m[4];
};
typedef struct lms7002m_lml_map lms7002m_lml_map_t;

int lms7002m_limelight_toggle_ntx(lms7002m_state_t* m);
int lms7002m_limelight_reset(lms7002m_state_t* m);
int lms7002m_limelight_fifo_reset(lms7002m_state_t* m, bool rx, bool tx);
int lms7002m_limelight_l_reset(lms7002m_state_t* m, bool rx, bool tx);

struct lms7002m_limelight_conf {
    uint8_t rxsisoddr : 1;
    uint8_t txsisoddr : 1;
    uint8_t ds_high : 1;
    uint8_t rx_ext_rd_fclk : 1;
    uint8_t rx_lfsr : 1;
    uint8_t rx_tx_dig_loopback : 1;
    uint8_t rx_port : 1; // 0 -- RXPORT=2 & TXPORT=1; 1 -- RXPORT=1 & TXPORT=2

    uint8_t rxdiv;
    uint8_t txdiv;
};
typedef struct lms7002m_limelight_conf lms7002m_limelight_conf_t;

int lms7002m_limelight_configure(lms7002m_state_t* m, lms7002m_limelight_conf_t params);
int lms7002m_limelight_map(lms7002m_state_t* m, lms7002m_lml_map_t l1m, lms7002m_lml_map_t l2m);

// CGEN
int lms7002m_cgen_disable(lms7002m_state_t* m);
int lms7002m_cgen_tune(lms7002m_state_t* m, unsigned fref, unsigned outfreq, unsigned txdiv_ord);

// SXX
enum lms7002m_sxx_path {
    SXX_RX = 0,
    SXX_TX,
};
typedef enum lms7002m_sxx_path lms7002m_sxx_path_t;

// NOTE: These functios preserve mac
int lms7002m_sxx_disable(lms7002m_state_t* m, lms7002m_sxx_path_t rx);
int lms7002m_sxx_tune(lms7002m_state_t* m, lms7002m_sxx_path_t rx, unsigned fref, unsigned lofreq, bool lochen);

// XBUF, AFE, LDO
int lms7002m_afe_enable(lms7002m_state_t* m, bool rxa, bool rxb, bool txa, bool txb);

// DCCAL
int lms7002m_dc_corr_en(lms7002m_state_t* m, bool rxa, bool rxb, bool txa, bool txb);
enum dc_param {
    DC_PARAM_FLAG_Q = 1,
    DC_PARAM_FLAG_B = 2,
    DC_PARAM_FLAG_RX = 4,

    P_TXA_I = 0,
    P_TXA_Q = 1,
    P_TXB_I = 2,
    P_TXB_Q = 3,
    P_RXA_I = 4,
    P_RXA_Q = 5,
    P_RXB_I = 6,
    P_RXB_Q = 7,
};
int lms7002m_dc_corr(lms7002m_state_t* m, unsigned p, int16_t v);

// CDS
// int lms7002m_cds_set(lms7002m_state_t* m, bool rxalml, bool rxblml);

// This functions is sensible to A/B channel selection
enum lms7002m_xxtsp {
    LMS_TXTSP,
    LMS_RXTSP,
};
typedef enum lms7002m_xxtsp lms7002m_xxtsp_t;

// TSTPs
int lms7002m_rxtsp_dc_corr(lms7002m_state_t* m, bool byp, unsigned wnd);
int lms7002m_xxtsp_bst(lms7002m_state_t* m, lms7002m_xxtsp_t tsp);
int lms7002m_xxtsp_enable(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, bool enable);
int lms7002m_xxtsp_int_dec(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, unsigned intdec_ord);

// XTSP data generator
enum lms7002m_xxtsp_gen {
    XXTSP_NORMAL,
    XXTSP_DC,
    XXTSP_TONE, // DCI - 0 -- FS, 1 -- -6dbFS; DCQ - 0 -- F/4; 1 -- F/8
};
typedef enum lms7002m_xxtsp_gen lms7002m_xxtsp_gen_t;

int lms7002m_xxtsp_gen(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, lms7002m_xxtsp_gen_t gen,
                       int16_t dci, int16_t dcq);

int lms7002m_xxtsp_cmix(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, int32_t freq);

int lms7002m_xxtsp_iq_gcorr(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, unsigned ig, unsigned qg);
int lms7002m_xxtsp_iq_phcorr(lms7002m_state_t* m, lms7002m_xxtsp_t tsp, int acorr);

// RFE
enum lms7002m_rfe_path {
    RFE_NONE = 0,
    RFE_LNAH,
    RFE_LNAL,
    RFE_LNAW,
};
typedef enum lms7002m_rfe_path lms7002m_rfe_path_t;

enum lms7002m_rfe_mode {
    RFE_MODE_DISABLE,
    RFE_MODE_NORMAL,
    RFE_MODE_LOOPBACKRF,
};
typedef enum lms7002m_rfe_mode lms7002m_rfe_mode_t;

int lms7002m_rfe_path(lms7002m_state_t* m, lms7002m_rfe_path_t p, lms7002m_rfe_mode_t mode);

enum lms7002m_rfe_gain {
    RFE_GAIN_LNA,
    RFE_GAIN_TIA,
    RFE_GAIN_RFB,
    RFE_GAIN_NONE, // Do not update gain values, just restore the gains
};
typedef enum lms7002m_rfe_gain lms7002m_rfe_gain_t;

// Attenuation in dB * 10, e.g. for 3dB 30 should be given
int lms7002m_rfe_gain(lms7002m_state_t* m, lms7002m_rfe_gain_t gain, int gainx10, int *goutx10);

// TRF
enum lms7002m_trf_path {
    TRF_MUTE,
    TRF_B1,
    TRF_B2,
};
typedef enum lms7002m_trf_path lms7002m_trf_path_t;

enum lms7002m_trf_mode {
    TRF_MODE_DISABLE,
    TRF_MODE_NORMAL,
    TRF_MODE_LOOPBACK,
};
typedef enum lms7002m_trf_mode lms7002m_trf_mode_t;

int lms7002m_trf_path(lms7002m_state_t* m,lms7002m_trf_path_t path, lms7002m_trf_mode_t mode);

enum lms7002m_trf_gain {
    TRF_GAIN_PAD,
    TRF_GAIN_LB,
};
typedef enum lms7002m_trf_gain lms7002m_trf_gain_t;

enum lms7002m_lb_loss {
    LB_LOSS_0 = 0,
    LB_LOSS_14 = 14,
    LB_LOSS_21 = 21,
    LB_LOSS_24 = 24,
};

int lms7002m_trf_gain(lms7002m_state_t* m, lms7002m_trf_gain_t gt, int gainx10, int *goutx10);

// RBB
enum lms7002m_rbb_path {
    RBB_LBF,
    RBB_HBF,
    RBB_BYP,
    RBB_PDET,
};
typedef enum lms7002m_rbb_path lms7002m_rbb_path_t;
enum lms7002m_rbb_mode {
    RBB_MODE_DISABLE,
    RBB_MODE_NORMAL,
    RBB_MODE_LOOPBACK,
    // RBB_MODE_EXT_ADC,  TODO
};
typedef enum lms7002m_rbb_mode lms7002m_rbb_mode_t;

int lms7002m_rbb_path(lms7002m_state_t* m, lms7002m_rbb_path_t path, lms7002m_rbb_mode_t mode);

int lms7002m_rbb_pga(lms7002m_state_t* m, int gainx10);

int lms7002m_rbb_lpf_raw(lms7002m_state_t* m, lms7002m_lpf_params_t params);

// Calculate default lpf filter parameters before calibration
int lms7002m_rbb_lpf_def(unsigned bw, bool lpf_l, lms7002m_lpf_params_t *params);


// TBB
enum lms7002m_tbb_path {
    TBB_BYP,
    TBB_LAD,
    TBB_HBF,
};
typedef enum lms7002m_tbb_path lms7002m_tbb_path_t;
enum lms7002m_tbb_mode {
    TBB_MODE_DISABLE,
    TBB_MODE_NORMAL,
    TBB_MODE_LOOPBACK,
    TBB_MODE_LOOPBACK_SWAPIQ,
};
typedef enum lms7002m_tbb_mode lms7002m_tbb_mode_t;

int lms7002m_tbb_path(lms7002m_state_t* m, lms7002m_tbb_path_t path, lms7002m_tbb_mode_t mode);

int lms7002m_tbb_lpf_def(unsigned bw, bool lpf_l, lms7002m_lpf_params_t *params);
int lms7002m_tbb_lpf_raw(lms7002m_state_t* m, lms7002m_lpf_params_t params);

int lms7002m_tbb_gain(lms7002m_state_t* m, int8_t gain);

// Calibration functions
enum lms7002m_vco_comparator {
    LMS7002M_VCO_LOW = 0,
    LMS7002M_VCO_FAIL = 1,
    LMS7002M_VCO_OK = 2,
    LMS7002M_VCO_HIGH = 3,
};

// RFE - TRF loopback control
lms7002m_trf_path_t lms7002m_trf_from_rfe_path(lms7002m_rfe_path_t rfe_path);

#endif
