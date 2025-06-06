// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef XSDR_CTRL_H
#define XSDR_CTRL_H

#include <stdint.h>
#include <usdr_port.h>
#include <usdr_lowlevel.h>

#include "../dev_param.h"
#include "../generic_usdr/generic_regs.h"
#include "lms7002m_ctrl.h"
#include "../hw/lms8001/lms8001.h"

#define RFIC_CHANS 2

enum xsdr_devices {
    SSDR_DEV = 0x31,
    XSDR_DEV = 0x30,
    XTRX_DEV = 0x2e,
};


// ===================================================================
// Frequency       LMS7 DAC/ADC     LML interface        Baseband
// FREF -> CGEN --> /rxcgen_div ---> /rxtsp_div  ---> /rx_host_decim
//          +-----> /txcgen_div ---> /txtsp_div  ---> /tx_host_inter
//
//                   rfic_*            dspfe_*        (user functions)
struct xsdr_dev
{
    lms7002_dev_t base;
    lms8001_state_t lms8;

    uint32_t hwid; // Standard harware feature bits

    uint8_t dsp_rxcfg  : 4;
    uint8_t dsp_txcfg  : 4;

    uint8_t hwchans_rx;
    uint8_t hwchans_tx;

    unsigned s_rxrate;
    unsigned s_txrate;
    unsigned s_adcclk;
    unsigned s_dacclk;
    unsigned s_flags;

    unsigned lms7_lob;

    bool afe_active;
    bool siso_sdr_active_rx;
    bool siso_sdr_active_tx;
    bool pwr_en;
    bool new_rev;
    bool ssdr;
    union {
        bool pmic_ch145_valid;
        bool dac_old_r5;
    };
};

typedef struct xsdr_dev xsdr_dev_t;

// API
int xsdr_set_samplerate(xsdr_dev_t *d,
                        unsigned rxrate, unsigned txrate,
                        unsigned adcclk, unsigned dacclk);


int xsdr_set_samplerate_ex(xsdr_dev_t *d,
                           unsigned rxrate, unsigned txrate,
                           unsigned adcclk, unsigned dacclk,
                           unsigned flags);

int xsdr_set_rx_port_switch(xsdr_dev_t *d, unsigned path);
int xsdr_set_tx_port_switch(xsdr_dev_t *d, unsigned path);

int xsdr_rfic_streaming_up(xsdr_dev_t *d, unsigned dir,
                           unsigned rx_chs, unsigned rx_flags,
                           unsigned tx_chs, unsigned tx_flags);

int xsdr_rfic_streaming_down(xsdr_dev_t *d, unsigned dir);

int xsdr_rfic_bb_set_freq(xsdr_dev_t *d,
                          unsigned channel,
                          bool dir_tx,
                          int64_t freq);

int xsdr_rfic_bb_set_badwidth(xsdr_dev_t *d,
                              unsigned channel,
                              bool dir_tx,
                              unsigned bw,
                              unsigned* actualbw);

int xsdr_rfic_set_gain(xsdr_dev_t *d,
                       unsigned channel,
                       unsigned gain_type,
                       int gain,
                       double *actualgain);

int xsdr_rfic_fe_set_freq(xsdr_dev_t *d,
                          unsigned channel,
                          unsigned type,
                          double freq,
                          double *actualfreq);

int xsdr_rfic_fe_set_lna(xsdr_dev_t *d,
                         unsigned channel,
                         //unsigned dir,
                         unsigned lna);


int xsdr_rfic_streaming_xflags(xsdr_dev_t *d,
                               unsigned xor_rx_flags,
                               unsigned xor_tx_flags);

// TODO add subdev for chaining
int xsdr_ctor(lldev_t dev, xsdr_dev_t *d);
int xsdr_init(xsdr_dev_t *d);

int xsdr_dtor(xsdr_dev_t *d);

int xsdr_set_extref(xsdr_dev_t *d, bool ext, uint32_t freq);


// Enable RFIC, no streaming
int xsdr_pwren(xsdr_dev_t *d, bool on);

int xsdr_prepare(xsdr_dev_t *d, bool rxen, bool txen);

int xsdr_gettemp(xsdr_dev_t *d, int* temp256);

enum xsdr_tx_port_cfg_flags {
    MUTE_B = 0,
    MUTE_A = 1,
    SWAP_AB = 2,
};
int xsdr_tx_antennat_port_cfg(xsdr_dev_t *d, unsigned mask);

int xsdr_trim_dac_vctcxo(xsdr_dev_t *d, uint16_t val);

// Calibration helpers
int xsdrcal_set_nco_offset(void* param, int channel, int32_t freqoffset);
int xsdrcal_set_corr_param(void* param, int channel, int corr_type, int value);
int xsdrcal_do_meas_nco_avg(void* param, int channel, unsigned logduration, int *func);
//int (*set_tx_testsig_fs8)(void* param, int channel);


int xsdr_phy_tune(xsdr_dev_t *d, unsigned val);
int xsdr_clk_debug_info(xsdr_dev_t *d);

int xsdr_hwchans_cnt(xsdr_dev_t *d, bool rx, unsigned chans);

enum {
    XSDR_CAL_RXLO = 1,
    XSDR_CAL_TXLO = 2,
    XSDR_CAL_RXIQIMB = 4,
    XSDR_CAL_TXIQIMB = 8,

    /* TX is externally feed to TX */
    XSDR_CAL_EXT_FB = 256,

    XSDR_DONT_SETBACK = 65536,
};


int xsdr_usbclk(xsdr_dev_t *d, bool uclk);

int xsdr_calibrate(xsdr_dev_t *d, unsigned channel, unsigned param, int* sarray);

int xsdr_trspi_lms8(xsdr_dev_t *d, uint32_t out, uint32_t* in);

#ifndef NO_IGPO

enum {
    IGPO_LMS_PWR_LDOEN = BIT(0),
    IGPO_LMS_PWR_RXEN = BIT(1),
    IGPO_LMS_PWR_TXEN = BIT(2),
    IGPO_LMS_PWR_NRESET = BIT(3),
};

enum {
    IGPO_LMS_PWR    = 0,
    IGPO_CLK_CFG    = 1,
    IGPO_TXSW       = 2,
    IGPO_RXSW       = 3,

    IGPO_CLKMEAS    = 5,
    IGPO_USB2_CFG   = 6,
    IGPO_GPS        = 7,
    IGPO_IOVCCSEL   = 8,
    IGPO_SMSIGIO    = 9,
    IGPO_DSP_RST    = 14,
    IGPO_LMS8_CTRL  = 15,

    IGPO_USB_CLK_EN = 16,
    IGPO_LDOLMS_EN  = 17,
    IGPO_LED        = 18,
    IGPO_PHYCAL     = 19,
};

enum {
    // 0 through 15 are generic
    IGPI_MEAS_RXCLK  = 16,
    IGPI_MEAS_TXCLK  = 20,
    IGPI_CLK1PPS     = 24,
};

#endif

enum {
    SPI_LMS7 = 0,
};





#endif
