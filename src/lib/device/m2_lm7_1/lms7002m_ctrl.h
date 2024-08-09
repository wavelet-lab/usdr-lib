// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMS7002M_CTRL_H
#define LMS7002M_CTRL_H

#include <stdint.h>
#include <usdr_port.h>
#include <usdr_lowlevel.h>

#include "../dev_param.h"
#include "../hw/lms7002m/lms7002m.h"

#define RFIC_CHANS 2

enum {
    MAX_TX_BANDS = 2,
    MAX_RX_BANDS = 3,
};

enum rfic_lms7_rf_path {
    XSDR_RX_L,
    XSDR_RX_H,
    XSDR_RX_W,

    XSDR_RX_L_TX_B2_LB,  // Loopback activates both RX and TX path in B2 => L configuration
    XSDR_RX_W_TX_B1_LB,
    XSDR_RX_H_TX_B1_LB,

    XSDR_TX_B1,
    XSDR_TX_B2,
    XSDR_TX_W,
    XSDR_TX_H,

    XSDR_RX_AUTO,
    XSDR_TX_AUTO,

    XSDR_RX_ADC_EXT,
};
typedef enum rfic_lms7_rf_path rfic_lms7_rf_path_t;

enum rfic_lms7_gain_types {
    RFIC_LMS7_RX_LNA_GAIN,
    RFIC_LMS7_RX_TIA_GAIN,
    RFIC_LMS7_RX_PGA_GAIN,
    RFIC_LMS7_RX_LB_GAIN,
    RFIC_LMS7_TX_PAD_GAIN,
    RFIC_LMS7_TX_LB_GAIN,
    RFIC_LMS7_TX_PGA_GAIN,
};

enum rfic_lms7_tune_types {
    RFIC_LMS7_TUNE_RX_FDD,
    RFIC_LMS7_TUNE_TX_FDD,
    RFIC_LMS7_TX_AND_RX_TDD,
};

enum {
    RFIC_LMS7_TX = BIT(0),
    RFIC_LMS7_RX = BIT(1),
};

struct lms7002_dev;
typedef struct lms7002_dev lms7002_dev_t;

typedef int (*on_change_antenna_port_sw_t)(lms7002_dev_t* dev, int direction, unsigned sw);
typedef const lms7002m_lml_map_t (*on_get_lml_portcfg_t)(bool rx, unsigned chs, unsigned flags, bool no_siso_map);

struct lms7002_dev
{
    lms7002m_state_t lmsstate;

    // Callbacks
    on_change_antenna_port_sw_t on_ant_port_sw;
    on_get_lml_portcfg_t on_get_lml_portcfg;

    // RFIC state
    uint8_t rx_cfg_path;  // Configuration index in cfg_auto_rx
    uint8_t tx_cfg_path;  // Configuration index in cfg_auto_tx
    uint8_t trf_lb_loss  : 2;
    uint8_t trf_lb_atten : 6;
    //uint8_t rfe_lb_atten;
    uint8_t tx_loss[2]; // TX loss setiing for A & B

    // Stream params
    uint8_t rxcgen_div;
    uint8_t txcgen_div;
    uint8_t rxtsp_div;
    uint8_t txtsp_div;
    uint8_t tx_host_inter;
    uint8_t rx_host_decim;

    uint8_t rx_no_siso_map;
    uint8_t tx_no_siso_map;

    rfic_lms7_rf_path_t rx_rfic_path;
    rfic_lms7_rf_path_t tx_rfic_path;

    bool rx_run[RFIC_CHANS];
    bool tx_run[RFIC_CHANS];

    opt_u32_t tx_bw[RFIC_CHANS];
    opt_u32_t rx_bw[RFIC_CHANS];

    opt_u32_t tx_dsp[RFIC_CHANS];
    opt_u32_t rx_dsp[RFIC_CHANS];

    unsigned fref; // Reference clock
    unsigned cgen_clk; // LMS7002 CGEN frequency
    unsigned rx_lo;
    unsigned tx_lo;

    lms7002m_limelight_conf_t lml_mode;

    lms7002m_lml_map_t map_rx;
    lms7002m_lml_map_t map_tx;

    unsigned lml_rx_chs;
    unsigned lml_rx_flags;
    unsigned lml_tx_chs;
    unsigned lml_tx_flags;

    freq_auto_band_map_t cfg_auto_rx[MAX_RX_BANDS];
    freq_auto_band_map_t cfg_auto_tx[MAX_TX_BANDS];

    bool rx_lna_lb_active;
};

int lms7002m_rbb_bandwidth(lms7002_dev_t *d, unsigned bw, bool loopback);
int lms7002m_tbb_bandwidth(lms7002_dev_t *d, unsigned bw, bool loopback);

int lms7002m_init(lms7002_dev_t* d, lldev_t dev, unsigned subdev, unsigned refclk);



int lms7002m_set_gain(lms7002_dev_t *d,
                     unsigned channel,
                     unsigned gain_type,
                     int gain,
                     double *actualgain);

int lms7002m_fe_set_freq(lms7002_dev_t *d,
                       unsigned channel,
                       unsigned type,
                       double freq,
                       double *actualfreq);

int lms7002m_rfe_set_path(lms7002_dev_t *d,
                          rfic_lms7_rf_path_t path);

int lms7002m_tfe_set_path(lms7002_dev_t *d,
                          rfic_lms7_rf_path_t path);

int lms7002m_fe_set_lna(lms7002_dev_t *d,
                         unsigned channel,
                         unsigned lna);


int lms7002m_bb_set_badwidth(lms7002_dev_t *d,
                             unsigned channel,
                             bool dir_tx,
                             unsigned bw,
                             unsigned* actualbw);

int lms7002m_bb_set_freq(lms7002_dev_t *d,
                        unsigned channel,
                        bool dir_tx,
                        int64_t freq);

int lms7002m_streaming_down(lms7002_dev_t *d, unsigned dir);

enum rfic_chan_flags {
    RFIC_SWAP_AB = BIT(0),
    RFIC_SWAP_IQ = BIT(1),
    RFIC_SISO_MODE = BIT(2),
    RFIC_SISO_SWITCH = BIT(3),

    // Test flags
    RFIC_SWAP_IQB = BIT(16),
    RFIC_SWAP_IQA = BIT(15),

    RFIC_LFSR = BIT(12),
    RFIC_DIGITAL_LB = BIT(11),

    // Algo
    RFIC_NO_DC_COMP = BIT(23),
};

int lms7002m_streaming_up(lms7002_dev_t *d, unsigned dir,
                          lms7002m_mac_mode_t rx_chs, unsigned rx_flags,
                          lms7002m_mac_mode_t tx_chs, unsigned tx_flags);


enum {
    XSDR_SR_MAXCONVRATE = 1,
    XSDR_SR_EXTENDED_CGEN = 2,
    XSDR_LML_SISO_DDR_RX = 4,
    XSDR_LML_EXT_FIFOCLK_RX = 8,
    XSDR_LML_EXT_FIFOCLK_TX = 16,
    XSDR_LML_SISO_DDR_TX = 32,
};

int lms7002m_samplerate(lms7002_dev_t *d,
                        unsigned rxrate, unsigned txrate,
                        unsigned adcclk, unsigned dacclk,
                        unsigned flags, const bool rx_port_1);


enum {
    XSDR_LMLRX_NORMAL = 0,
    XSDR_LMLRX_DIGLOOPBACK = 1,
    XSDR_LMLRX_LFSR = 2,
};

int lms7002m_set_lmlrx_mode(lms7002_dev_t *d, unsigned mode);


// Calibration

int lms7002m_set_corr_param(lms7002_dev_t* d, int channel, int corr_type, int value);
int lms7002m_set_tx_testsig(lms7002_dev_t* d, int channel, int32_t freqoffset, unsigned pwr);

#endif
