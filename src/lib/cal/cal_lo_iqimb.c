// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "cal_lo_iqimb.h"
#include <usdr_port.h>
#include <usdr_logging.h>
#include <limits.h>

static int _evaluate_rxtxlo(void* obj, int param, int val, int* func, bool rx, int mulf)
{
    struct calibrate_ops* ops = (struct calibrate_ops*)obj;
    unsigned cp = (rx) ? CORR_DIR_RX : CORR_DIR_TX;
    int res;

    switch (param) {
    case 0: cp |= CORR_PARAM_I; break;
    case 1: cp |= CORR_PARAM_Q; break;
    case 2: cp |= CORR_PARAM_A; break;
    case 3: cp |= CORR_PARAM_GIQ; break;
    default:
        return -EINVAL;
    }

    res = ops->set_corr_param(ops->param, ops->channel, cp, val);
    if (res)
        return res;

    // just set, don't measure
    if (func == NULL)
        return 0;

    res = ops->do_meas_nco_avg(ops->param, ops->channel, ops->deflogdur * mulf, func);
    if (res)
        return res;

    return 0;
}

static int _evaluate_rxlo(void* obj, int param, int val, int* func)
{
    return _evaluate_rxtxlo(obj, param, val, func, true, 1);
}

static int _evaluate_txlo(void* obj, int param, int val, int* func)
{
    return _evaluate_rxtxlo(obj, param, val, func, false, 1);
}

static int _evaluate_txlo_precise(void* obj, int param, int val, int* func)
{
    return _evaluate_rxtxlo(obj, param, val, func, false, 4);
}

static int _evaluate_rxaiq(void* obj, int param, int val, int* func)
{
    return _evaluate_rxtxlo(obj, 2 + param, val, func, true, 1);
}

static int _evaluate_txaiq(void* obj, int param, int val, int* func)
{
    return _evaluate_rxtxlo(obj, 2 + param, val, func, false, 1);
}

int calibrate_rxlo(struct calibrate_ops* ops)
{
    int res;
    struct opt_iteration2d o;
    o.limit[0] = ops->rxlo_iq_corr;
    o.limit[1] = ops->rxlo_iq_corr;
    o.func = _evaluate_rxlo;
    o.sf = &find_golden_min;
    o.exparam = 0;

    res = find_best_2d(&o, 1, ops, ops->defstop, &ops->i, &ops->q, &ops->bestmeas);
    if (res)
        return res;

    return 0;
}

int calibrate_txlo(struct calibrate_ops* ops)
{
    int res;
    struct opt_iteration2d o[4];
    o[0].limit[0] = ops->txlo_iq_corr;
    o[0].limit[1] = ops->txlo_iq_corr;
    o[0].func = _evaluate_txlo;
    o[0].sf = &find_golden_min;
    o[0].exparam = 0;
    o[1].limit[0].max = ops->txlo_iq_corr.max / 8;
    o[1].limit[0].min = ops->txlo_iq_corr.min / 8;
    o[1].limit[1].max = ops->txlo_iq_corr.max / 8;
    o[1].limit[1].min = ops->txlo_iq_corr.min / 8;
    o[1].func = _evaluate_txlo;
    o[1].sf = &find_golden_min;
    o[1].exparam = 0;
    o[2].limit[0].max = 80;
    o[2].limit[0].min = -80;
    o[2].limit[1].max = 80;
    o[2].limit[1].min = -80;
    o[2].func = _evaluate_txlo;
    o[2].sf = &find_iterate_min;
    o[2].exparam = 4;
    o[3].limit[0].max = 8;
    o[3].limit[0].min = -8;
    o[3].limit[1].max = 8;
    o[3].limit[1].min = -8;
    o[3].func = _evaluate_txlo_precise;
    o[3].sf = &find_iterate_min;
    o[3].exparam = 0;

    // Set TX freq
    res =  ops->set_tx_testsig(ops->param, ops->channel, 0, 0);
    if (res)
        return res;

    // Set RX to be TXLO - sampl
    int32_t freqoff = (((int64_t)ops->rxsamplerate * ops->rxtxlo_frac) >> 31);

    USDR_LOG("UDEV", USDR_LOG_WARNING, "CAL_TXLO: Set RX measeure freq %d - %d (from %.3f)\n",
             ops->txfrequency, freqoff, ops->rxtxlo_frac / (float)INT_MAX);

    res = ops->set_corr_param(ops->param, ops->channel, CORR_DIR_RX | CORR_OP_SET_FREQ,
                              ops->txfrequency - freqoff);
    if (res)
        return res;

    res = ops->set_nco_offset(ops->param, ops->channel, ((-ops->rxtxlo_frac) << 1));
    if (res)
        return res;

    res = find_best_2d(&o[0], SIZEOF_ARRAY(o), ops, ops->defstop, &ops->i, &ops->q, &ops->bestmeas);
    if (res)
        return res;

    return 0;
}

static int _calibrate_txpwr(struct calibrate_ops* ops, int32_t freqoffset, int* opwr)
{
    int ampl = 128;
    int pwr_r;
    int res;

    for (; ampl <= 32768; ampl <<= 1) {
        res = ops->set_tx_testsig(ops->param, ops->channel, freqoffset, ampl - 1);
        if (res)
            return res;

        res = ops->do_meas_nco_avg(ops->param, ops->channel, 0, &pwr_r);
        if (res)
            return res;

        USDR_LOG("UDEV", USDR_LOG_WARNING, "CAL_IQIMB: Amp %d => %d pwr\n", ampl, pwr_r);
        if (pwr_r > -7000)
            break;
    }

    *opwr = pwr_r;
    return 0;
}

int _calibrate_iqimb_generic(struct calibrate_ops* ops, int32_t freqoffset,
                             int32_t rximoff, int32_t rxreoff, evaluate_fnN_t func)
{
    int res;
    int pwr_r;
    int pwr_i;
    int a, giq, b;
    struct opt_iteration2d o[3];

    res = ops->set_nco_offset(ops->param, ops->channel, (rxreoff << 1));
    if (res)
        return res;

    res = _calibrate_txpwr(ops, freqoffset, &pwr_r);
    if (res)
        return res;

    res = ops->set_nco_offset(ops->param, ops->channel, (rximoff << 1));
    if (res)
        return res;

    res = ops->do_meas_nco_avg(ops->param, ops->channel, 0, &pwr_i);
    if (res)
        return res;

    USDR_LOG("UDEV", USDR_LOG_WARNING, "CAL_IQIMB: Imbalance %d pwr (%d)\n", pwr_i, pwr_r - pwr_i);
    // Probe AI and AQ, choice what path to go

    o[0].limit[0] = ops->rximb_ang_corr;
    o[0].limit[1] = ops->rximb_iq_corr;
    o[0].func = func;
    o[0].sf = &find_golden_min;
    o[0].exparam = 0;
    o[1].limit[0] = ops->rximb_ang_corr;
    o[1].limit[1] = ops->rximb_iq_corr;
    o[1].func = func;
    o[1].sf = &find_golden_min;
    o[1].exparam = 0;
    o[2].limit[0].max = 8;
    o[2].limit[0].min = -8;
    o[2].limit[1].max = 8;
    o[2].limit[1].min = -8;
    o[2].func = func;
    o[2].sf = &find_iterate_min;
    o[2].exparam = 0;

    res = find_best_2d(&o[0], SIZEOF_ARRAY(o), ops, ops->defstop, &a, &giq, &b);
    if (res)
        return res;

    USDR_LOG("UDEV", USDR_LOG_WARNING, "CAL_IQIMB: Imbalance %d pwr (%d) improvement %d [PHA=%d IQ=%d]\n", b, pwr_r - b, pwr_i - b, a, giq);

    return 0;
}

int calibrate_rxiqimb(struct calibrate_ops* ops)
{
    int res;
#if 0
    int pwr_r;
    int pwr_i;
    int a, giq, b;
    struct opt_iteration2d o[3];
#endif
    // Set RX to be TXLO - sampl
    int32_t freqoff = (((int64_t)ops->rxsamplerate * ops->rxiqimb_frac) >> 31);

    USDR_LOG("UDEV", USDR_LOG_WARNING, "CAL_RXIQIMB: Set RX measeure freq %d - %d (from %.3f)\n",
             ops->rxfrequency, freqoff, ops->rxiqimb_frac / (float)INT_MAX);

    res = ops->set_corr_param(ops->param, ops->channel, CORR_DIR_TX | CORR_OP_SET_FREQ,
                              ops->rxfrequency + freqoff);
    if (res)
        return res;

    return _calibrate_iqimb_generic(ops, 0, ops->rxiqimb_frac, -ops->rxiqimb_frac, _evaluate_rxaiq);
#if 0
    res = ops->set_corr_param(ops->param, ops->channel, CORR_DIR_TX | CORR_OP_SET_FREQ,
                              ops->rxfrequency + freqoff);
    if (res)
        return res;

    res = ops->set_nco_offset(ops->param, ops->channel, ((-ops->rxiqimb_frac) << 1));
    if (res)
        return res;

    res = _calibrate_txpwr(ops, &pwr_r);

    res = ops->set_nco_offset(ops->param, ops->channel, ((ops->rxiqimb_frac) << 1));
    if (res)
        return res;

    res = ops->do_meas_nco_avg(ops->param, ops->channel, 0, &pwr_i);
    if (res)
        return res;

    USDR_LOG("UDEV", USDR_LOG_WARNING, "CAL_IQIMB: Imbalance %d pwr (%d)\n", pwr_i, pwr_r - pwr_i);
    // Probe AI and AQ, choice what path to go

    o[0].limit[0] = ops->rximb_ang_corr;
    o[0].limit[1] = ops->rximb_iq_corr;
    o[0].func = _evaluate_rxaiq;
    o[0].sf = &find_golden_min;
    o[0].exparam = 0;
    o[1].limit[0] = ops->rximb_ang_corr;
    o[1].limit[1] = ops->rximb_iq_corr;
    o[1].func = _evaluate_rxaiq;
    o[1].sf = &find_golden_min;
    o[1].exparam = 0;
    o[2].limit[0].max = 8;
    o[2].limit[0].min = -8;
    o[2].limit[1].max = 8;
    o[2].limit[1].min = -8;
    o[2].func = _evaluate_rxaiq;
    o[2].sf = &find_golden_min;
    o[2].exparam = 0;

    res = find_best_2d(&o[0], SIZEOF_ARRAY(o), ops, ops->defstop, &a, &giq, &b);
    if (res)
        return res;

    USDR_LOG("UDEV", USDR_LOG_WARNING, "CAL_IQIMB: Imbalance %d pwr (%d) improvement %d\n", b, pwr_r - b, pwr_i - b);
    return 0;
#endif
}


int calibrate_txiqimb(struct calibrate_ops* ops)
{
    int res;
    // Set RX to be TXLO - sampl
    int32_t freqoff = (((int64_t)ops->rxsamplerate * ops->txiqimb_frac) >> 31);

    USDR_LOG("UDEV", USDR_LOG_WARNING, "CAL_TXIQIMB: Set RX measeure freq %d - %d (from %.3f)\n",
             ops->txfrequency, freqoff, ops->txiqimb_frac / (float)INT_MAX);

    res = ops->set_corr_param(ops->param, ops->channel, CORR_DIR_RX | CORR_OP_SET_FREQ,
                              ops->txfrequency - freqoff);
    if (res)
        return res;

    return _calibrate_iqimb_generic(ops,
                                    ops->rxiqimb_frac,
                                    -ops->txiqimb_frac + ops->rxiqimb_frac,
                                    -ops->txiqimb_frac - ops->rxiqimb_frac,
                                    _evaluate_txaiq);
}
