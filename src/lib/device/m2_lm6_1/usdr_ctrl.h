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

#define USDR_RX_AUTO 255
#define USDR_TX_AUTO 255

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
    GAIN_RX_VGA2A,
    GAIN_RX_VGA2B,
    GAIN_RX_AUTO,

    GAIN_TX_VGA1,
    GAIN_TX_VGA2,
    GAIN_TX_AUTO,
};

#define MAX_NCO_STREAMS 2

struct freq_data
{
    opt_u32_t lo[MAX_NCO_STREAMS];
    opt_u32_t bb[MAX_NCO_STREAMS];
};
typedef struct freq_data freq_data_t;


enum {
    AMP_COMP_2CH_3DB = 0,
    AMP_COMP_2CH_0DB = 1,
    AMP_COMP_1CH_3DB = 2,
    AMP_COMP_1CH_0DB = 3,
};


#define IMB_AMPL_MAX 262144
#define IMB_AMPL_MIN -262144

#define IMB_PHASE_MAX 45000
#define IMB_PHASE_MIN -45000

struct imb_data
{
    int32_t ampl;     // AMPL_IMB_MIN  .. AMPL_IMB_MAX
    int32_t pahse;    // IMB_PHASE_MIN .. IMB_PHASE_MAX
    int32_t amp_corr; // Amplitude correction
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

    unsigned si_vco_div;
    unsigned si_vco_freq;

    lms6002d_state_t lms;
    unsigned refclkpath;
    unsigned fref;

    // Configured stream count
    uint8_t rx_lchans;
    uint8_t tx_lchans;

    uint8_t rx_cfg_path;
    uint8_t tx_cfg_path;

    uint8_t rx_rfic_path;
    uint8_t rx_rfic_lna;
    uint8_t tx_rfic_path;
    uint8_t tx_rfic_band;

    // Gain settings
    uint8_t rx_lna;
    uint8_t rx_vga1;
    uint8_t rx_vga2a;
    uint8_t rx_vga2b;

    bool rx_run;
    bool tx_run;
    bool rx_pwren;
    bool tx_pwren;

    bool mexir_en;
    bool vio_boost;
    bool has_rxchain;
    bool has_txchain;

    bool rf_loopback_active;

    unsigned rawsamplerate;
    unsigned rxbb_decim;
    unsigned txbb_intr;

    unsigned dac_clk; // Use for NCO offset calculation
    unsigned adc_clk; // Use for NCO offset calculation

    unsigned rx_lo;
    unsigned tx_lo;

    unsigned rx_nco_distance; // Maximum distance from LO to the farest NCO
    unsigned tx_nco_distance;

    freq_data_t rx_raw;
    freq_data_t tx_raw;

    unsigned mixer_lo;
    unsigned rfic_rx_lo;

    opt_u32_t tx_bw;
    opt_u32_t rx_bw;

    struct imb_data tx_corr;

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

int usdr_set_lob_freq(struct usdr_dev *d, unsigned freqlob);

int usdr_rfic_fe_set_rxlna(struct usdr_dev *d,
                           const char* lna, bool lb);
int usdr_rfic_fe_set_txlna(struct usdr_dev *d,
                           const char *lna);

enum fe_freq_type {
    FE_FREQ_LO_RX = 0,
    FE_FREQ_LO_TX = 1,
    FE_FREQ_BB_RX = 2,
    FE_FREQ_BB_TX = 3,
};

int usdr_rfic_fe_set_freq(struct usdr_dev *d,
                          enum fe_freq_type type,
                          unsigned chmask,
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

int usdr_calib_dc(struct usdr_dev *d, bool rx);

int usdr_gettemp(struct usdr_dev *d, int* temp256);

int usdr_reset_txfex(struct usdr_dev *d);

int usdr_rxdccorr(struct usdr_dev *d, uint64_t *ov);

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
    IGPO_DCCORR     = 9,

    // No longer used, replaced with direct PHY control
    // IGPO_DSP_RX_CTRL = 10,

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

int usdr_set_extref(usdr_dev_t *d, bool ext, uint32_t freq);

int usdr_tx_dccorr(usdr_dev_t *d, int16_t i, int16_t q);

// Realign NCO-A / NCO-B to be phase cocherent
int usdr_reset_txnco(struct usdr_dev *d);

int usdr_txupdate_cal(struct usdr_dev *d);

int usdr_tx_iqimb_set(usdr_dev_t* d, int iq_amp_imb, int phase_imb);


#endif

#endif
