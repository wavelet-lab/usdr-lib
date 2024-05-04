// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USDR_CTRL_H
#define USDR_CTRL_H

#include <stdint.h>
#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <math.h>

#include "../dev_param.h"
#include "../hw/lms6002d/lms6002d.h"
#include "../generic_usdr/generic_regs.h"


enum {
    USDR_MAX_TX_BANDS = 2,
    USDR_MAX_RX_BANDS = 3,
};

#define USDR_RX_AUTO 0
#define USDR_TX_AUTO 0

// enum rfic_lms6_rf_path_rx {
//     USDR_RX_AUTO = 0,
//     USDR_RX_W    = 1,
//     USDR_RX_H    = 2,
//     USDR_RX_EXT  = 3, // External / mixer
// };

// enum rfic_lms6_rf_path_tx {
//     USDR_TX_AUTO = 0,
//     USDR_TX_B1 = 1,
//     USDR_TX_B2 = 2,
// };


enum {
    RFIC_LMS6_TX = BIT(0),
    RFIC_LMS6_RX = BIT(1),
};

enum usdrrevs {
    USDR_REV_UNKNOWN = 0,
    USDR_REV_1 = 1,
    USDR_REV_2 = 2,
    USDR_REV_3 = 3,
    USDR_REV_4 = 4,
};

enum usdrgains {
    GAIN_RX_LNA,
    GAIN_RX_VGA1,
    GAIN_RX_VGA2,

    GAIN_TX_VGA1,
    GAIN_TX_VGA2,
};

struct usdr_dev
{
    union {
        lldev_t dev;
    } base;

    subdev_t subdev;
    uint32_t hwid; // Standard harware feature bits
    unsigned hw_board_rev;
    unsigned hw_board_hasmixer;

    unsigned si_vco_freq;

    lms6002d_state_t lms;
    unsigned refclkpath;
    unsigned fref;

    uint8_t rx_cfg_path;
    uint8_t tx_cfg_path;

    uint8_t rx_rfic_path;
    uint8_t rx_rfic_lna;
    uint8_t tx_rfic_path;
    uint8_t tx_rfic_band;

    unsigned dsp_clk;

    unsigned rx_lo;
    unsigned tx_lo;

    bool rx_run;
    bool tx_run;
    bool rx_pwren;
    bool tx_pwren;

    bool mexir_en;
    unsigned mixer_lo;
    unsigned rfic_rx_lo;

    opt_u32_t tx_bw;
    opt_u32_t rx_bw;

    opt_u32_t tx_dsp;
    opt_u32_t rx_dsp;

    freq_auto_band_map_t cfg_auto_rx[USDR_MAX_RX_BANDS];
    freq_auto_band_map_t cfg_auto_tx[USDR_MAX_TX_BANDS];
};
typedef struct usdr_dev usdr_dev_t;

enum {
    // Use maximum internal interpolation/decimation
    USDR_SR_MAXCONVRATE = 1,
};

int usdr_rfic_streaming_up(struct usdr_dev *d, unsigned dir);
int usdr_rfic_streaming_down(struct usdr_dev *d, unsigned dir);

int usdr_set_samplerate_ex(struct usdr_dev *d,
                           unsigned rxrate, unsigned txrate,
                           unsigned adcclk, unsigned dacclk,
                           unsigned flags);

int usdr_set_rx_port_switch(struct usdr_dev *d, unsigned path);
int usdr_set_tx_port_switch(struct usdr_dev *d, unsigned path);


int usdr_rfic_fe_set_rxlna(struct usdr_dev *d,
                           const char* lna);

int usdr_rfic_fe_set_freq(struct usdr_dev *d,
                          bool dir_tx,
                          double freq,
                          double *actualfreq);

int usdr_rfic_bb_set_badwidth(struct usdr_dev *d,
                              bool dir_tx,
                              unsigned bw,
                              unsigned* actualbw);

int usdr_rfic_set_gain(struct usdr_dev *d,
                       unsigned gain_type,
                       int gain,
                       int *actualgain);

int usdr_ctor(lldev_t dev, subdev_t sub, struct usdr_dev *d);
int usdr_init(struct usdr_dev *d, int ext_clk, unsigned int ext_fref);

int usdr_dtor(struct usdr_dev *d);

// int usdr_pwren(struct usdr_dev *d, bool on);

int usdr_lob_set(struct usdr_dev *d, unsigned freq);


int usdr_i2c_addr_ext_set(struct usdr_dev *d, uint8_t addr);

int usdr_ext_i2c(lldev_t dev, subdev_t subdev, unsigned ls_op, lsopaddr_t ls_op_addr,
                 size_t meminsz, void* pin, size_t memoutsz,
                 const void* pout);


int usdr_calib_dc(struct usdr_dev *d, bool rx);

int usdr_gettemp(struct usdr_dev *d, int* temp256);

#ifndef NO_IGPO

enum {
    IGPO_LMS_PWR_LDOEN = BIT(0),
    IGPO_LMS_PWR_RXEN = BIT(1),
    IGPO_LMS_PWR_TXEN = BIT(2),
    IGPO_LMS_PWR_NRESET = BIT(3),
};

enum {
    IGPO_LMS_RST    = 0,
    IGPO_RXMIX_EN   = 1,
    IGPO_TXSW       = 2,
    IGPO_RXSW       = 3,
    IGPO_DSP_RX_CFG = 4,
    IGPO_DSP_TX_CFG = 5,
    IGPO_USB2_CFG   = 6,
    IGPO_BOOSTER    = 7,
    IGPO_LED        = 8,

    IGPO_FRONT      = 15,
    IGPO_CLKMEAS    = 16,
    IGPO_ENABLE_OSC = 17,

    IGPI_USBS        = 16,
    IGPI_USBS2       = 20,
    IGPI_USBC        = 24,
    IGPI_CLK1PPS     = 28,
    IGPI_TXCLK       = 32,
    IGPI_RXCLK       = 36,
    IGPI_RX_I        = 40,
    IGPI_RX_Q        = 44,

};

#endif

#endif
