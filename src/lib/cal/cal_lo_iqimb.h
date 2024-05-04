// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CAL_LO_IQIMB_H
#define CAL_LO_IQIMB_H

#include <usdr_port.h>
#include "opt_func.h"

enum corr_params {
    CORR_PARAM_I = 0,
    CORR_PARAM_Q = 1,
    CORR_PARAM_A = 2,   // Angle (phase correction) for IQ imbalance
    CORR_PARAM_GIQ = 3, // Gain(-N..0) gain correction to Q (0..N) gain correction to I

    CORR_OP_SET_FREQ = 16,
    CORR_OP_SET_BW = 18,
    CORR_OP_SET_GAIN = 19,

    CORR_TYPE_OFF = 8,
    CORR_TYPE_DCOFF = (0 << CORR_TYPE_OFF),
    CORR_TYPE_IQIMB = (1 << CORR_TYPE_OFF),

    //CORR_CHAN_OFF = 16,

    CORR_DIR_OFF = 24,
    CORR_DIR_RX = (0 << CORR_DIR_OFF),
    CORR_DIR_TX = (1 << CORR_DIR_OFF),
};

struct calibrate_ops
{
    int adcrate;       // Actual ADC samplerate before decimation
    int rxsamplerate;  // RX samplerate after on-chip decimation
    int dacrate;       // Actual DAC samplerate before interpolation
    int txsamplerate;  // TX samplerate after on-chip decimation

    int rxfrequency;   // Desired Calibrated freq
    int txfrequency;   // Desired Calibrated freq
    int channel;
    int deflogdur;     // Number of BB cycles to integrate the value
    int defstop;

    int rxtxlo_frac;
    int rxiqimb_frac;  // Relative position (-1; 1) to feed test tx sig for RXIQIMB & TXLO
    int txiqimb_frac;  // Relative position (-1; 1) to feed test tx sig for TXIQIMB

    void* param;

    // limits
    struct opt_par_limits txlo_iq_corr;
    struct opt_par_limits rxlo_iq_corr;

    struct opt_par_limits tximb_iq_corr;
    struct opt_par_limits tximb_ang_corr;

    struct opt_par_limits rximb_iq_corr;
    struct opt_par_limits rximb_ang_corr;

    // calibrated values from last operation
    int i;
    int q;
    int a;
    int bestmeas;

    // functions
    int (*set_nco_offset)(void* param, int channel, int32_t freqoffset);
    int (*set_corr_param)(void* param, int channel, int corr_type, int value);
    int (*do_meas_nco_avg)(void* param, int channel, unsigned logduration, int* fout);
    int (*set_tx_testsig)(void* param, int channel, int32_t freqoffset, unsigned pwr);

    // private
};

// Prior calling these function we assume loopback is activated

int calibrate_rxlo(struct calibrate_ops* ops);
int calibrate_txlo(struct calibrate_ops* ops);

int calibrate_rxiqimb(struct calibrate_ops* ops);
int calibrate_txiqimb(struct calibrate_ops* ops);

int calibrate_rxbw(struct calibrate_ops* ops);
int calibrate_txbw(struct calibrate_ops* ops);


// TODO
// Calibrate VCO


#endif
