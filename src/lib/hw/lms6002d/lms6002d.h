// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMS6002D_H
#define LMS6002D_H

#include <usdr_lowlevel.h>

#define LPF_BANDS 16

struct lms6002d_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    unsigned fref; //PLL reference frequency

    // uint8_t rxpll_0x25;
    // uint8_t rxfe_0x71;
    // uint8_t rxfe_0x72;
    // uint8_t rxfe_0x75;

    // uint8_t topclk_0x09; // TOP_ENREG - DSM clock & cal clocks
    // uint8_t topclk_0x05; // TOP_ENCFG - TX/RX power down

    uint8_t rclpfcal[LPF_BANDS]; //Calibrated values for LPF

    uint8_t top_encfg;
    uint8_t top_enreg;
    uint8_t rxpll_vco_div_bufsel;
    uint8_t rfe_in1sel_dci;
    uint8_t rfe_gain_lna_sel;
};
typedef struct lms6002d_state lms6002d_state_t;


int lms6002d_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lms6002d_state_t* out);
int lms6002d_tune_pll(lms6002d_state_t* obj, bool tx, unsigned freq);

//int lms6002d_rf_enable(lms6002d_state_t* obj, bool tx, bool en);

int lms6002d_trf_enable(lms6002d_state_t* obj, bool en);
int lms6002d_rfe_enable(lms6002d_state_t* obj, bool en);

int lms6002d_rxvga2_enable(lms6002d_state_t* obj, bool en);

int lms6002d_set_bandwidth(lms6002d_state_t* obj, bool tx, unsigned freq);

enum lms6002d_rx_lna_gains {
    RXLNAGAIN_MAX = 3,
    RXLNAGAIN_MID = 2,
    RXLNAGAIN_BYPASS = 1, // For LNA1 & LNA2
    RXLNAGAIN_MAX_LNA3 = 0,
};

int lms6002d_set_rxlna_gain(lms6002d_state_t* obj, unsigned lnag);

int lms6002d_set_rxvga1_gain(lms6002d_state_t* obj, unsigned vga);

// Combined stage1 & stage 2
int lms6002d_set_rxvga2_gain(lms6002d_state_t* obj, unsigned vga);
// Individual satge1 & stage2  lsb = 3dB)
int lms6002d_set_rxvga2ab_gain(lms6002d_state_t* obj, unsigned vga2a, unsigned vga2b);

int lms6002d_set_txvga1_gain(lms6002d_state_t* obj, unsigned vga);
int lms6002d_set_txvga2_gain(lms6002d_state_t* obj, unsigned vga);

int lms6002d_set_rx_extterm(lms6002d_state_t* obj, bool extterm);

enum lms6002d_rx_path {
    RXPATH_OFF = 0,
    RXPATH_LNA1 = 1,
    RXPATH_LNA2 = 2,
    RXPATH_LNA3 = 3,
};

int lms6002d_set_rx_path(lms6002d_state_t* obj, unsigned path);

enum lms6002d_tx_path {
    TXPATH_OFF = 0,
    TXPATH_PA1 = 1,
    TXPATH_PA2 = 2,
    TXPATH_AUX = 3,
};
typedef enum lms6002d_tx_path lms6002d_tx_path_t;

int lms6002d_set_tx_path(lms6002d_state_t* obj, unsigned path);

int lms6002d_set_rxfedc(lms6002d_state_t* obj, int8_t dci, int8_t dcq);


int lms6002d_cal_lpf(lms6002d_state_t* obj);
int lms6002d_cal_txrxlpfdc(lms6002d_state_t* obj, bool tx);
int lms6002d_cal_vga2(lms6002d_state_t* obj);
int lms6002d_cal_lpf_bandwidth(lms6002d_state_t* obj, unsigned bcode);


// For TIA calibration
int lms6002d_set_tia_cfb(lms6002d_state_t* obj, uint8_t value);
int lms6002d_set_tia_rfb(lms6002d_state_t* obj, uint8_t value);


#endif
