// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dc_estim.h"
#include <math.h>

enum {
    ADDR_DC_EST_RESET = 0,
    ADDR_DC_EST_ACCM = 1,
};

// TODO: fix wr bit to 1 for usdr!
static int _phy_rx_reg(lldev_t d, uint32_t llreg, bool wr, uint8_t bank, uint8_t addr, uint16_t val)
{
    uint32_t reg = (!wr ? 0x80000000 : 0) | (((uint32_t)bank & 0x7f) << 24) | ((uint32_t)addr << 16) | val;
    return lowlevel_reg_wr32(d, 0, llreg, reg);
}


int phy_dc_estim_start(lldev_t d, uint32_t llreg, bool start)
{
    return _phy_rx_reg(d, llreg, true, PHY_REG_RXBANK_DC_EST, ADDR_DC_EST_RESET, start ? 0 : 1);
}

int phy_dc_estim_accum(lldev_t d, uint32_t llreg, unsigned accum)
{
    return _phy_rx_reg(d, llreg, true, PHY_REG_RXBANK_DC_EST, ADDR_DC_EST_ACCM, accum);
}

int phy_dc_estim_get(lldev_t d, uint32_t llreg, enum dc_estimations v, int32_t* odata)
{
    int res = 0;
    res = res ? res : _phy_rx_reg(d, llreg, false, PHY_REG_RXBANK_DC_EST, v, 0);
    res = res ? res : lowlevel_reg_rd32(d, 0, llreg, (uint32_t*)odata);
    return res;
}


// Calculate power in dbfs
int phy_rfe_pwrdc_get(lldev_t d, uint32_t llreg, unsigned acc_norm, int prev_gen, unsigned chan_no,
                      unsigned range, int *meas1000db)
{
    int32_t val[2];
    int gen, gen_n;
    int res = 0;

    do {
        res = res ? res : phy_dc_estim_get(d, llreg, DC_ESTIM_GEN, &gen);
        if (res)
            return res;

        // USDR_LL_LOG(d, "XDEV", USDR_LOG_INFO, "[]%d->%d %d %d\n", prev_gen, gen, val[0], val[1]);
        if (prev_gen == gen)
            return -EAGAIN;

        res = res ? res : phy_dc_estim_get(d, llreg, chan_no ? DC_ESTIM_BI : DC_ESTIM_AI, &val[0]);
        res = res ? res : phy_dc_estim_get(d, llreg, chan_no ? DC_ESTIM_BQ : DC_ESTIM_AQ, &val[1]);
        res = res ? res : phy_dc_estim_get(d, llreg, DC_ESTIM_GEN, &gen_n);
        if (res)
            return res;


    } while (gen != gen_n);

    double fs_i = val[0];
    double fs_q = val[1];
    double i = (0 + (fs_i / acc_norm / 65536)) / range; // Static correction by +0.5 bits in FPGA
    double q = (0 + (fs_q / acc_norm / 65536)) / range; // Static correction by +0.5 bits in FPGA
    double pwr_d = i * i + q * q;

    USDR_LL_LOG(d, "XDEV", USDR_LOG_INFO, "%d->%d %d %d => %.3f %.3f\n",
                prev_gen, gen, val[0], val[1], i * range, q * range);

    if (pwr_d <= 1e-18)
        pwr_d = 1e-18;

    *meas1000db = (1000 * 10 * log10(pwr_d));
    return 0;
}


int phy_do_meas_nco_avg(lldev_t d, uint32_t llreg, unsigned range, int channel, unsigned acc_idx, int* oaccum)
{
    int res = 0;
    int meas1000db = 0;
    int accum = 0, gen = 0;

    res = res ? res : phy_dc_estim_accum(d, llreg, acc_idx);
    res = res ? res : phy_dc_estim_start(d, llreg, false);
    res = res ? res : usleep(1);
    res = res ? res : phy_dc_estim_start(d, llreg, true);
    res = res ? res : phy_dc_estim_get(d, llreg, DC_ESTIM_GEN, &gen);
    if (res)
        return res;

    for (unsigned k = 0; k < 8000; k++) {
        res = phy_rfe_pwrdc_get(d, llreg, acc_idx, gen, channel, range, &meas1000db);
        if (res != -EAGAIN) {
            accum += meas1000db;
            break;
        }

        usleep(1000);
    }

    USDR_LL_LOG(d, "XDEV", USDR_LOG_INFO, "MEAS[%d] = %.3f\n", channel, meas1000db/1e3);
    *oaccum = accum;
    return res;
}

