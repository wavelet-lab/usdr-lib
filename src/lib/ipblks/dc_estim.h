// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DC_ESTIM_H
#define DC_ESTIM_H

#include <stdint.h>
#include <usdr_port.h>
#include <usdr_lowlevel.h>


enum dc_estimations {
    DC_ESTIM_GEN = 0,
    DC_ESTIM_AI = 4,
    DC_ESTIM_AQ = 5,
    DC_ESTIM_BI = 6,
    DC_ESTIM_BQ = 7,
};

enum {
    PHY_REG_RXBANK_DC_EST  = 6, // DC estimator
};

int phy_dc_estim_start(lldev_t d, uint32_t llreg, bool start);
int phy_dc_estim_accum(lldev_t d, uint32_t llreg, unsigned accum);
int phy_dc_estim_get(lldev_t d, uint32_t llreg, enum dc_estimations v, int32_t* odata);
int phy_rfe_pwrdc_get(lldev_t d, uint32_t llreg, unsigned acc_norm, int prev_gen, unsigned chan_no,
                      unsigned range, int *meas1000db);
int phy_do_meas_nco_avg(lldev_t d, uint32_t llreg, unsigned range, int channel, unsigned acc_idx, int* oaccum);


#endif
