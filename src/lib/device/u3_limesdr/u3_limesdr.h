// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef _MP_LM7_1_GPS_H
#define _MP_LM7_1_GPS_H



#define BIT(x) (1u << (x))

enum rfic_lms7_rf_path {
    XSDR_RX_L,
    XSDR_RX_H,
    XSDR_RX_W,

    XSDR_RX_L_LB,
    XSDR_RX_W_LB,
    XSDR_RX_H_LB,

    XSDR_TX_H,
    XSDR_TX_W,

    XSDR_RX_AUTO,
    XSDR_TX_AUTO,

    XSDR_RX_ADC_EXT,

    XSDR_RX_LB_AUTO,
};

enum rfic_lms7_gain_types {
    RFIC_LMS7_RX_LNA_GAIN,
    RFIC_LMS7_RX_TIA_GAIN,
    RFIC_LMS7_RX_PGA_GAIN,
    RFIC_LMS7_RX_LB_GAIN,
    RFIC_LMS7_TX_PAD_GAIN,
    RFIC_LMS7_TX_LB_GAIN
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

enum rfic_chans {
    RFIC_CHAN_A = BIT(0),
    RFIC_CHAN_B = BIT(1),
    RFIC_CHAN_AB = BIT(1) | BIT(0),
};

enum rfic_chan_flags {
    RFIC_SWAP_AB = BIT(0),
    RFIC_SWAP_IQ = BIT(1),
    RFIC_SISO_MODE = BIT(2),
    RFIC_SISO_SWITCH = BIT(3),

    // Test flags
    RFIC_SWAP_IQB = BIT(16),
    RFIC_SWAP_IQA = BIT(15),
    RFIC_TEST_SIG_A = BIT(14),
    RFIC_TEST_SIG_B = BIT(13),
    RFIC_LFSR = BIT(12),
    RFIC_DIGITAL_LB = BIT(11),

    // Algo
    RFIC_NO_DC_COMP = BIT(23),
};

enum sigtype {
    XSDR_TX_LO_CHANGED,
    XSDR_RX_LO_CHANGED,
    XSDR_TX_LNA_CHANGED,
    XSDR_RX_LNA_CHANGED,
};


#endif
