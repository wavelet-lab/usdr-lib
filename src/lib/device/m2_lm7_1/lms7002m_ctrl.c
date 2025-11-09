// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT


#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include <usdr_logging.h>
#include "lms7002m_ctrl.h"

#include "../cal/cal_lo_iqimb.h"

#ifndef MAX
#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#endif

enum sigtype {
    XSDR_TX_LO_CHANGED,
    XSDR_RX_LO_CHANGED,
    XSDR_TX_LNA_CHANGED,
    XSDR_RX_LNA_CHANGED,
};

static unsigned _ulog(unsigned d)
{
    switch (d) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    case 16: return 4;
    }
    return 5;
}

static int get_antenna_cfg_by_name(const char* name, const freq_auto_band_map_t* maps, unsigned max)
{
    for (unsigned i = 0; i < max; i++) {
        if (strcasecmp(name, maps[i].name0) == 0)
            return i;
        if (strcasecmp(name, maps[i].name1) == 0)
            return i;
    }

    return -1;
}

static int get_antenna_cfg_by_freq(unsigned freq, const freq_auto_band_map_t* maps, unsigned max)
{
    unsigned i;
    for (i = 0; i < max - 1; i++) {
        if (freq < maps[i].stop_freq)
            return i;
    }

    return i;
}

static int get_antenna_cfg_by_band(unsigned band, const freq_auto_band_map_t* maps, unsigned max)
{
    for (unsigned i = 0; i < max; i++) {
        if (maps[i].band == band)
            return i;
    }

    return -1;
}

static inline double clamp(double i, double min, double max)
{
    if (i < min)
        return min;
    else if (i > max)
        return max;
    return i;
}

int lms7002m_init(lms7002_dev_t* d, lldev_t dev, unsigned subdev, unsigned refclk)
{
    d->lmsstate.dev = dev;
    d->lmsstate.subdev = subdev;
    d->fref = refclk;

    d->rx_rfic_path = XSDR_RX_AUTO;
    d->tx_rfic_path = XSDR_TX_AUTO;
    d->rx_cfg_path = 0;
    d->tx_cfg_path = 0;

    return 0;
}

static int _lms7002m_check_chan(unsigned chan)
{
    if (chan > LMS7_CH_AB)
        return -EINVAL;
    else if (chan == LMS7_CH_NONE)
        return -EINVAL;

    return 0;
}

static int _lms7002m_set_lna_rx(lms7002_dev_t *d, unsigned cfg_idx)
{
    const freq_auto_band_map_t* cfg = &d->cfg_auto_rx[cfg_idx];
    lms7002m_rfe_path_t band = (lms7002m_rfe_path_t)cfg->band;
    lms7002m_trf_path_t txlbband = TRF_MUTE;
    d->rx_cfg_path = cfg_idx;
    if (d->rx_lna_lb_active) {
        txlbband = lms7002m_trf_from_rfe_path(band);
    }

    USDR_LOG("LMS7", USDR_LOG_INFO, "%s: Set RX band to %d (%s/%s) %s [TXLB:%d => ATEEN=%d,%d]\n",
             lowlevel_get_devname(d->lmsstate.dev), band,
             cfg->name0, cfg->name1,
             d->rx_lna_lb_active ? "loopback enabled" : "",
             txlbband, d->trf_lb_atten, d->trf_lb_loss);

    int res = lms7002m_rfe_path(&d->lmsstate,
                                band, //((d->rx_rfic_lna = band)),
                                d->rx_lna_lb_active ? RFE_MODE_LOOPBACKRF : RFE_MODE_NORMAL);
    if (d->rx_lna_lb_active) {
        res = (res) ? res : lms7002m_trf_path(&d->lmsstate,
                                              txlbband, //((d->tx_rfic_band = txlbband)),
                                              TRF_MODE_LOOPBACK);
    }
    if (res)
        return res;

    return d->on_ant_port_sw(d, RFIC_LMS7_RX, d->rx_lna_lb_active ? cfg->swlb : cfg->sw);
}


static int _lms7002m_set_lna_tx(lms7002_dev_t *d, unsigned cfg_idx)
{
    const freq_auto_band_map_t* cfg = &d->cfg_auto_tx[cfg_idx];
    lms7002m_trf_path_t band = (lms7002m_trf_path_t)cfg->band;
    int res = 0;
    d->tx_cfg_path = cfg_idx;

    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: Set TX band to %d (%s/%s)\n",
             lowlevel_get_devname(d->lmsstate.dev), band, cfg->name0, cfg->name1);

    if (d->rx_lna_lb_active) {
        res = lms7002m_rfe_path(&d->lmsstate,
                                band == 1 ? RFE_LNAW : RFE_LNAL,
                                RFE_MODE_LOOPBACKRF);
    }

    res = (res) ? res : lms7002m_trf_path(&d->lmsstate,
                                          band, //((d->tx_rfic_band = band)),
                                          d->rx_lna_lb_active ? TRF_MODE_LOOPBACK : TRF_MODE_NORMAL);
    if (res)
        return res;

    return d->on_ant_port_sw(d, RFIC_LMS7_TX, d->rx_lna_lb_active ? cfg->swlb : cfg->sw);
}

#if 0   //unused, DO NOT DELETE
static
const char* get_band_name(unsigned l)
{
    switch (l) {
    case RFE_NONE: return "NONE";
    case RFE_LNAH: return "LNAH";
    case RFE_LNAL: return "LNAL";
    case RFE_LNAW: return "LNAW";
    }
    return "<unknown>";
}
#endif

static int _lms7002m_signal_event(lms7002_dev_t *d, enum sigtype t)
{
    int res = 0;
    int cfgidx;

    switch (t) {
    case XSDR_RX_LO_CHANGED:
        res = lms7002m_mac_set(&d->lmsstate, LMS7_CH_AB);
    case XSDR_RX_LNA_CHANGED:
        if (d->rx_rfic_path == XSDR_RX_AUTO) {
            cfgidx = get_antenna_cfg_by_freq(d->rx_lo, d->cfg_auto_rx, MAX_RX_BANDS);
            USDR_LOG("XDEV", USDR_LOG_INFO, "%s: Auto RX band selection: %s\n",
                        lowlevel_get_devname(d->lmsstate.dev), d->cfg_auto_rx[cfgidx].name0);

            res = (res) ? res : _lms7002m_set_lna_rx(d, cfgidx);
        }
        return res;
    case XSDR_TX_LO_CHANGED:
        res = lms7002m_mac_set(&d->lmsstate, LMS7_CH_AB);
    case XSDR_TX_LNA_CHANGED:
        if (d->tx_rfic_path == XSDR_TX_AUTO) {
            cfgidx = get_antenna_cfg_by_freq(d->tx_lo, d->cfg_auto_tx, MAX_TX_BANDS);
            USDR_LOG("XDEV", USDR_LOG_INFO, "%s: Auto TX band selection: %s\n",
                        lowlevel_get_devname(d->lmsstate.dev), d->cfg_auto_tx[cfgidx].name0);

            res = (res) ? res : _lms7002m_set_lna_tx(d, cfgidx);
        }
        return res;
    }

    return 0;
}



int lms7002m_set_gain(lms7002_dev_t *d,
                     unsigned channel,
                     unsigned gain_type,
                     int gain,
                     double *actualgain)
{
    double actual;
    int aret;
    int res = _lms7002m_check_chan(channel);
    if (res)
        return res;
    unsigned ogain = gain;
    static const char* s_gains[] = {
        "RX_LNA",
        "RX_TIA",
        "RX_PGA",
        "RX_LB",
        "TX_PAD",
        "TX_LB",
        "TX_PGA",
        "{invalid}",
    };
    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: Set gain %s to %d on %d channel\n",
             lowlevel_get_devname(d->lmsstate.dev),
             s_gains[MIN(gain_type, SIZEOF_ARRAY(s_gains))],
             gain,
             channel);
    res = lms7002m_mac_set(&d->lmsstate, channel);
    if (res)
        return res;

    switch (gain_type) {
    case RFIC_LMS7_RX_LNA_GAIN:
        res = lms7002m_rfe_gain(&d->lmsstate, RFE_GAIN_LNA, (gain - 30) * 10, &aret);
        actual = (aret + 300) / 10;
        break;
    case RFIC_LMS7_RX_TIA_GAIN:
        //gain = -clamp(gain, -12, 0);
        //res = lms7002m_rfe_tia(&d->lmsstate, gain, &aret);
        //actual = -aret;
        res = lms7002m_rfe_gain(&d->lmsstate, RFE_GAIN_TIA, gain * 10, &aret);
        actual = aret / 10;
        break;
    case RFIC_LMS7_RX_PGA_GAIN:
        gain = clamp(gain, 0, 31);
        actual = gain;
        res = lms7002m_rbb_pga(&d->lmsstate, gain * 10);
        break;
    case RFIC_LMS7_RX_LB_GAIN:
        res = lms7002m_rfe_gain(&d->lmsstate, RFE_GAIN_RFB, gain * 10, &aret);
        actual = aret / 10;

        //gain = -clamp(gain, -40, 0);
        //d->rfe_lb_atten = gain * 4;
        //res = lms7002m_rfe_lblna(&d->lmsstate, d->rfe_lb_atten, &aret);
        //actual = -(double)aret/4.0;
        break;
    case RFIC_LMS7_TX_PAD_GAIN:
        if (gain > 0)
            gain = 0;
        actual = gain;

        res = lms7002m_trf_gain(&d->lmsstate, TRF_GAIN_PAD, -10 * gain, &aret);
        if (channel & LMS7_CH_A)
            d->tx_loss[0] = aret / -10;
        if (channel & LMS7_CH_B)
            d->tx_loss[1] = aret / -10;
        break;
    case RFIC_LMS7_TX_LB_GAIN:
        if (gain > 0)
            gain = 0;
        actual = gain;

        d->trf_lb_atten = -gain;
        d->trf_lb_loss = 0;
        //res = lms7002m_trf_gain(&d->lmsstate, d->trf_lb_atten, d->trf_lb_loss);
        res = lms7002m_trf_gain(&d->lmsstate, TRF_GAIN_LB, -10 * gain, &aret);
        break;
    case RFIC_LMS7_TX_PGA_GAIN:
        gain = clamp(gain, -62, 63);
        actual = 0;

        res = lms7002m_tbb_gain(&d->lmsstate, gain);
        break;
    default:
        return -EINVAL;
    }
    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: Set gain %d (%d) to %d on %d channel => actual = %.3f\n",
                lowlevel_get_devname(d->lmsstate.dev), gain, ogain, gain_type, channel, actual / 1e6);

    if (actualgain)
        *actualgain = actual;
    return res;
}

int lms7002m_fe_set_freq(lms7002_dev_t *d,
                       unsigned channel,
                       unsigned type,
                       double freq,
                       double *actualfreq)
{
    int res;
    double res_freq = 0;
    lms7002m_sxx_path_t path;

    switch (type) {
    case RFIC_LMS7_TUNE_RX_FDD:
        path = SXX_RX;
        break;
    case RFIC_LMS7_TUNE_TX_FDD:
    case RFIC_LMS7_TX_AND_RX_TDD:
        path = SXX_TX;
        break;
    default: return -EINVAL;
    }

    if (freq == 0.0) {
        lms7002m_sxx_disable(&d->lmsstate, path);
        if (actualfreq)
            *actualfreq = 0.0;
        return 0;
    }

    if (type == RFIC_LMS7_TX_AND_RX_TDD) {
        lms7002m_sxx_disable(&d->lmsstate, SXX_RX);
    }

    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: FE_FREQ path=%d type=%d freq=%f ch=%d\n",
                lowlevel_get_devname(d->lmsstate.dev), path, type, freq, channel);


    res = lms7002m_sxx_tune(&d->lmsstate, path, d->fref, (unsigned)freq,
                            type == RFIC_LMS7_TX_AND_RX_TDD);
    res_freq = freq; //TODO !!!!!
    if (res) {
        return res;
    }

    if (actualfreq)
        *actualfreq = res_freq;

    if (type == RFIC_LMS7_TX_AND_RX_TDD) {
        d->rx_lo = d->tx_lo = res_freq;
    } else {
        if (path == SXX_TX) {
            d->tx_lo = res_freq;
        } else {
            d->rx_lo = res_freq;
        }
    }

    if ((type == RFIC_LMS7_TX_AND_RX_TDD || type == RFIC_LMS7_TUNE_RX_FDD) &&
            (d->rx_run[0] || d->rx_run[1])) {
        res = _lms7002m_signal_event(d, XSDR_RX_LO_CHANGED);
        if (res)
            return res;
    }
    if ((type == RFIC_LMS7_TX_AND_RX_TDD || type == RFIC_LMS7_TUNE_TX_FDD) &&
            (d->tx_run[0] || d->tx_run[1])) {
        res = _lms7002m_signal_event(d, XSDR_TX_LO_CHANGED);
        if (res)
            return res;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FIXME:
static int _lms7002m_lb_status_changes(lms7002_dev_t *d)
{
    int res = 0;
    if (!d->rx_lna_lb_active) {
        USDR_LOG("UDEV", USDR_LOG_WARNING, "%s: Turning off loopback\n",
                 lowlevel_get_devname(d->lmsstate.dev));
        res = (res) ? res : lms7002m_trf_gain(&d->lmsstate,
                                              TRF_GAIN_PAD,
                                              -10 * ((d->lmsstate.reg_mac & 1) ? d->tx_loss[0] : d->tx_loss[1]),
                                              NULL);
        res = (res) ? res : _lms7002m_set_lna_tx(d, d->tx_cfg_path );
        //res = (res) ? res : lms7002m_rfe_update_gains(&d->lmsstate);
    } else {
        //int lb_loss = 0;
        res = (res) ? res : lms7002m_trf_gain(&d->lmsstate,
                                              TRF_GAIN_LB,
                                              0, NULL);
        res = (res) ? res : lms7002m_trf_gain(&d->lmsstate,
                                              TRF_GAIN_PAD, //TRF_GAIN_LB,
                                              -10 * d->trf_lb_atten,
                                              NULL);
        // res = (res) ? res : lms7002m_rfe_gain(&d->lmsstate,
        //                                        d->rfe_lb_atten, &lb_loss);

        USDR_LOG("UDEV", USDR_LOG_WARNING, "%s: Turning on loopback RX + TX loss: -- + %d dB\n",
                 lowlevel_get_devname(d->lmsstate.dev), /*lb_loss,*/ d->trf_lb_atten);
    }

    // Update LNA / LOOPBACK gain after LB activated / deactivated
    res = (res) ? res : lms7002m_rfe_gain(&d->lmsstate, RFE_GAIN_NONE, 0, NULL);
    return res;
}

int lms7002m_rfe_set_path(lms7002_dev_t *d,
                          rfic_lms7_rf_path_t path)
{
    int res = 0;
    unsigned band;
    bool lb = false;
    bool lb_change = false;
    int cfgidx;

    switch (path) {
    case XSDR_RX_L: band = RFE_LNAL; break;
    case XSDR_RX_H: band = RFE_LNAH; break;
    case XSDR_RX_W: band = RFE_LNAW; break;

    case XSDR_RX_L_TX_B2_LB: band = RFE_LNAL; lb = true; break;
    case XSDR_RX_W_TX_B1_LB: band = RFE_LNAW; lb = true; break;
    case XSDR_RX_H_TX_B1_LB: band = RFE_LNAH; lb = true; break;

    case XSDR_RX_AUTO: break;

    case XSDR_RX_ADC_EXT:
        USDR_LOG("UDEV", USDR_LOG_INFO, "%s: Activating external ADC input NOT IMPLEMENTED\n",
                 lowlevel_get_devname(d->lmsstate.dev));
        return -EINVAL;

    default:
        USDR_LOG("UDEV", USDR_LOG_WARNING, "%s: Unknown FE path %d\n",
                 lowlevel_get_devname(d->lmsstate.dev), path);
        return -EINVAL;
    }

    if (d->rx_lna_lb_active != lb) {
        d->rx_lna_lb_active = lb;
        lb_change = true;
    }

    d->rx_rfic_path = path;
    if (path == XSDR_RX_AUTO) {
        cfgidx = get_antenna_cfg_by_freq(d->rx_lo, d->cfg_auto_rx, MAX_RX_BANDS);
    } else {
        cfgidx = get_antenna_cfg_by_band(band, d->cfg_auto_rx, MAX_RX_BANDS);
    }
    if (cfgidx < 0)
        return -EINVAL;


    res = (res) ? res : _lms7002m_set_lna_rx(d, cfgidx);
    if (lb_change) {
        res = (res) ? res : _lms7002m_lb_status_changes(d);
    }
    return res;
}

int lms7002m_tfe_set_path(lms7002_dev_t *d,
                          rfic_lms7_rf_path_t path)
{
    int cfgidx;

    switch (path) {
    case XSDR_TX_B1:
    case XSDR_TX_B2:
        cfgidx = get_antenna_cfg_by_band(path == XSDR_TX_B1 ? 1 : 2, d->cfg_auto_tx, MAX_TX_BANDS);
        break;
    case XSDR_TX_W:
    case XSDR_TX_H:
        cfgidx = get_antenna_cfg_by_name(path == XSDR_TX_W ? "W" : "H", d->cfg_auto_tx, MAX_TX_BANDS);
        break;
    case XSDR_TX_AUTO:
        d->tx_rfic_path = XSDR_TX_AUTO;
        d->rx_lna_lb_active = false;
        return _lms7002m_signal_event(d, XSDR_TX_LNA_CHANGED);
    default:
        USDR_LOG("UDEV", USDR_LOG_WARNING, "%s: Unknown FE path %d\n",
                 lowlevel_get_devname(d->lmsstate.dev), path);
        return -EINVAL;
    }

    if (cfgidx < 0)
        return -EINVAL;

    d->tx_rfic_path = path;
    return _lms7002m_set_lna_tx(d, cfgidx);
}

int lms7002m_fe_set_lna(lms7002_dev_t *d,
                         unsigned channel,
                         unsigned lna)
{
    int res;
    res = lms7002m_mac_set(&d->lmsstate, channel);
    if (res)
        return res;

    switch (lna) {
    case XSDR_TX_B1:
    case XSDR_TX_B2:
    case XSDR_TX_AUTO:
        return lms7002m_tfe_set_path(d, (rfic_lms7_rf_path_t)lna);
    default:
        break;
    }

    return lms7002m_rfe_set_path(d, (rfic_lms7_rf_path_t)lna);
}


int lms7002m_rbb_bandwidth(lms7002_dev_t *d, unsigned bw, bool loopback)
{
    lms7002m_lpf_params_t p;
    lms7002m_rbb_path_t path = (bw > 200e6) ? RBB_BYP : (bw > 20e6) ? RBB_HBF : RBB_LBF;
    int res = 0;

    res = (res) ? res : lms7002m_rbb_lpf_def(bw, path == RBB_LBF, &p);
    res = (res) ? res : lms7002m_rbb_path(&d->lmsstate, path, loopback ? RBB_MODE_LOOPBACK : RBB_MODE_NORMAL);
    res = (res) ? res : lms7002m_rbb_lpf_raw(&d->lmsstate, p);

    return res;
}

int lms7002m_tbb_bandwidth(lms7002_dev_t *d, unsigned bw, bool loopback)
{
    lms7002m_lpf_params_t p;
    lms7002m_tbb_path_t path = (bw > 200e6) ? TBB_BYP : (bw > 20e6) ? TBB_HBF : TBB_LAD;
    int res = 0;

    res = (res) ? res : lms7002m_tbb_lpf_def(bw, path == TBB_LAD, &p);
    res = (res) ? res : lms7002m_tbb_path(&d->lmsstate, path, loopback ? TBB_MODE_LOOPBACK : TBB_MODE_NORMAL);
    res = (res) ? res : lms7002m_tbb_lpf_raw(&d->lmsstate, p);

    return res;
}

int lms7002m_bb_set_badwidth(lms7002_dev_t *d,
                             unsigned channel,
                             bool dir_tx,
                             unsigned bw,
                             unsigned* actualbw)
{
    int res;
    res = _lms7002m_check_chan(channel);
    if (res)
        return res;

    for (int j = LMS7_CH_A; j <= LMS7_CH_B; j++) {
        if (channel == LMS7_CH_A && j == LMS7_CH_B)
            continue;
        else if (channel == LMS7_CH_B && j == LMS7_CH_A)
            continue;

        res = lms7002m_mac_set(&d->lmsstate, j);
        if (res)
            return res;

        if (!dir_tx) {
            opt_u32_set_val(&d->rx_bw[(j == LMS7_CH_A) ? 0 : 1], bw);

            res = lms7002m_rbb_bandwidth(d, bw, false);
            if (actualbw)
                *actualbw = bw;
        } else {
            opt_u32_set_val(&d->tx_bw[(j == LMS7_CH_A) ? 0 : 1], bw);

            res = lms7002m_tbb_bandwidth(d, bw, false);
            if (actualbw)
                *actualbw = bw;
        }
    }

    return res;
}



int lms7002m_bb_set_freq(lms7002_dev_t *d,
                        unsigned channel,
                        bool dir_tx,
                        int64_t freq)
{
    int res;
    const char* devstr = lowlevel_get_devname(d->lmsstate.dev);

    res = _lms7002m_check_chan(channel);
    if (res)
        return res;

    opt_u32_t* dsp_f = dir_tx ? &d->tx_dsp[0] : &d->rx_dsp[0];
    double conv_freq = d->cgen_clk / (dir_tx ? d->txcgen_div : d->rxcgen_div);
    double rel_freq = freq / conv_freq;
    if (rel_freq > 0.5 || rel_freq < -0.5) {
        USDR_LOG("XDEV", USDR_LOG_WARNING,
                 "%s: NCO %s ouf of range, requested %.3f while DAC %.3f\n",
                 devstr, dir_tx ? "TX" : "RX",
                 rel_freq / 1000, conv_freq / 1000);
        return -EINVAL;
    }
    int pfreq = rel_freq * 4294967296;
    if (channel & LMS7_CH_A)
        opt_u32_set_val(&dsp_f[0], pfreq);
    if (channel & LMS7_CH_B)
        opt_u32_set_val(&dsp_f[1], pfreq);

    if ((dir_tx && (d->tx_run[0] || d->tx_run[1])) || (!dir_tx && (d->rx_run[0] || d->rx_run[1]))) {
        res = lms7002m_mac_set(&d->lmsstate, channel);
        if (res)
            return res;
        res = lms7002m_xxtsp_cmix(&d->lmsstate, dir_tx ? LMS_TXTSP : LMS_RXTSP,
                                  channel == LMS7_CH_B ? dsp_f[1].value : dsp_f[0].value);
        if (res)
            return res;
    }

    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: NCO ch=%d type=%d freq=%lld\n",
                devstr, channel, dir_tx, (long long)freq);
    return 0;
}



int lms7002m_streaming_down(lms7002_dev_t *d, unsigned dir)
{
    // SET MAC
    lms7002m_mac_set(&d->lmsstate, LMS7_CH_AB);

    int res;
    if (dir & RFIC_LMS7_RX) {
        lms7002m_xxtsp_enable(&d->lmsstate, LMS_RXTSP, false);
        lms7002m_rfe_path(&d->lmsstate, RFE_NONE, RFE_MODE_DISABLE);
        d->rx_run[0] = false;
        d->rx_run[1] = false;
    }

    if (dir & RFIC_LMS7_TX) {
        lms7002m_xxtsp_enable(&d->lmsstate, LMS_TXTSP, false);
        lms7002m_trf_path(&d->lmsstate, TRF_MUTE, TRF_MODE_DISABLE);
        d->tx_run[0] = false;
        d->tx_run[1] = false;
    }

    res = lms7002m_afe_enable(&d->lmsstate,
                              d->rx_run[0], d->rx_run[1],
                              d->tx_run[0], d->tx_run[1]);
    if (res)
        return res;

    return 0;
}


static lms7002m_mac_mode_t _corr_ch(lms7002m_mac_mode_t mode,
                                   unsigned flags)
{
    if (((mode == LMS7_CH_AB) && (flags & RFIC_SISO_MODE)) && (!(flags & RFIC_SISO_SWITCH))) {
        if (flags & RFIC_SWAP_AB) {
            mode = LMS7_CH_B;
        } else {
            mode = LMS7_CH_A;
        }
    }

    return mode;
}

int lms7002m_streaming_up(lms7002_dev_t *d, unsigned dir,
                          lms7002m_mac_mode_t rx_chs_i, unsigned rx_flags,
                          lms7002m_mac_mode_t tx_chs_i, unsigned tx_flags)
{
    unsigned rx_chs, tx_chs;
    bool rxafen_a = d->rx_run[0];
    bool rxafen_b = d->rx_run[1];

    bool txafen_a = d->tx_run[0];
    bool txafen_b = d->tx_run[1];
    int res;
    unsigned ich;
    const char* devstr = lowlevel_get_devname(d->lmsstate.dev);

    if (dir & RFIC_LMS7_RX) {
        if (_lms7002m_check_chan(rx_chs_i)) {
            return -EINVAL;
        }
        rx_chs = _corr_ch(rx_chs_i, rx_flags);
        //d->chprx = params->rx;
        d->lml_rx_chs = rx_chs;
        d->lml_rx_flags = rx_flags;
        d->map_rx = d->on_get_lml_portcfg(true, d->lml_rx_chs, d->lml_rx_flags, false /* d->rx_no_siso_map */);

        rxafen_a = rx_chs != LMS7_CH_B;
        rxafen_b = rx_chs != LMS7_CH_A;
    }
    if (dir & RFIC_LMS7_TX) {
        if (_lms7002m_check_chan(tx_chs_i)) {
            return -EINVAL;
        }
        tx_chs = _corr_ch(tx_chs_i, tx_flags);
        //d->chptx = params->tx;
        d->lml_tx_chs = tx_chs;
        d->lml_tx_flags = tx_flags;
        d->map_tx = d->on_get_lml_portcfg(false, d->lml_tx_chs, d->lml_tx_flags, false /* d->tx_no_siso_map */);

        txafen_a = tx_chs != LMS7_CH_B;
        txafen_b = tx_chs != LMS7_CH_A;
    }

    res = lms7002m_limelight_map(&d->lmsstate,
                                 d->lml_mode.rx_port == 1 ? d->map_rx : d->map_tx,
                                 d->lml_mode.rx_port == 1 ? d->map_tx : d->map_rx);
    if (res)
        return res;

    res = lms7002m_afe_enable(&d->lmsstate,
                              rxafen_a || rxafen_b, rxafen_b,
                              txafen_a || txafen_b, txafen_b);
    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: AFE TX=[%d;%d] RX=[%d;%d]\n",
                devstr, txafen_a, txafen_b, rxafen_a, rxafen_b);

    if (dir & RFIC_LMS7_RX) {
        res = res ? res : lms7002m_mac_set(&d->lmsstate, rx_chs);
        res = res ? res : lms7002m_xxtsp_int_dec(&d->lmsstate, LMS_RXTSP,_ulog(d->rxtsp_div));
        res = res ? res : lms7002m_xxtsp_enable(&d->lmsstate, LMS_RXTSP, true);
        res = res ? res : lms7002m_rbb_path(&d->lmsstate, RBB_LBF, RBB_MODE_NORMAL); //should be set from bandwidth function
        res = res ? res : lms7002m_rxtsp_dc_corr(&d->lmsstate,
                                                 (rx_flags & RFIC_NO_DC_COMP) ? true : false, 7);
        if (res)
            return res;

        // Run flags must be set before selecting path
        d->rx_run[0] = rxafen_a;
        d->rx_run[1] = rxafen_b;
        res = (d->rx_rfic_path == XSDR_RX_AUTO) ?
                  _lms7002m_signal_event(d, XSDR_RX_LNA_CHANGED) :
                  _lms7002m_set_lna_rx(d, d->rx_cfg_path);
        if (res)
            return res;

        // Restore settings
        for (ich = 0; ich < 2; ich++) {
            unsigned bandwidth;
            int32_t freqoffset;

            lms7002m_mac_mode_t lch = (ich == 0) ? LMS7_CH_A : LMS7_CH_B;
            if (!(rx_chs & lch))
                continue;

            res = lms7002m_mac_set(&d->lmsstate, lch);
            if (res)
                return res;

            if (d->rx_bw[ich].set) {
                bandwidth = d->rx_bw[ich].value;
                USDR_LOG("XDEV", USDR_LOG_INFO, "%s: RBB Restore BW[%d]=%d\n",
                            devstr, ich, d->rx_bw[ich].value);
            } else {
                bandwidth = d->cgen_clk / d->rxcgen_div / d->rxtsp_div / d->rx_host_decim;
                USDR_LOG("XDEV", USDR_LOG_INFO, "%s: No RBB[%d] was set; defaulting to current rx samplerate %u\n",
                            devstr, ich, bandwidth);
            }
            res = lms7002m_rbb_bandwidth(d, bandwidth, false);
            if (res)
                return res;

            if (d->rx_dsp[ich].set) {
                USDR_LOG("XDEV", USDR_LOG_INFO,  "%s: RBB Restore DSP[%d]=%d\n",
                            devstr, ich, d->rx_dsp[ich].value);
                freqoffset = d->rx_dsp[ich].value;
            } else {
                freqoffset = 0;
            }
            res = lms7002m_xxtsp_cmix(&d->lmsstate, LMS_RXTSP, freqoffset);
            if (res)
                return res;
        }
    }
    if (dir & RFIC_LMS7_TX) {
        res = res ? res : lms7002m_mac_set(&d->lmsstate, tx_chs);
        res = res ? res : lms7002m_xxtsp_int_dec(&d->lmsstate, LMS_TXTSP,_ulog(d->txtsp_div));
        res = res ? res : lms7002m_xxtsp_enable(&d->lmsstate, LMS_TXTSP, true);
        res = res ? res : lms7002m_tbb_path(&d->lmsstate, TBB_LAD, TBB_MODE_NORMAL);  //should be set from bandwidth function
        res = res ? res : lms7002m_trf_path(&d->lmsstate, TRF_MUTE, TRF_MODE_NORMAL); // signal function will select proper band
        if (res)
            return res;

        // Restore settings
        for (ich = 0; ich < 2; ich++) {
            unsigned bandwidth;
            int32_t freqoffset;

            lms7002m_mac_mode_t lch = (ich == 0) ? LMS7_CH_A : LMS7_CH_B;
            if (!(tx_chs & lch))
                continue;

            res = lms7002m_mac_set(&d->lmsstate, lch);
            if (res)
                return res;

            if (d->tx_bw[ich].set) {
                USDR_LOG("XDEV", USDR_LOG_INFO, "%s: TBB Restore BW[%d]=%d\n",
                            devstr, ich, d->tx_bw[ich].value);
                bandwidth = d->tx_bw[ich].value;
            } else {
                bandwidth = d->cgen_clk / d->txcgen_div / d->txtsp_div / d->tx_host_inter;
                USDR_LOG("XDEV", USDR_LOG_INFO, "%s: No TBB[%d] was set; defaulting to current rx samplerate %u\n",
                            devstr, ich, bandwidth);
            }
            res = lms7002m_tbb_bandwidth(d, bandwidth, false);
            if (res)
                return res;

            if (d->tx_dsp[ich].set) {
                USDR_LOG("XDEV", USDR_LOG_INFO, "%s: TBB Restore DSP[%d]=%d\n",
                            devstr, ich, d->tx_dsp[ich].value);
                freqoffset = d->tx_dsp[ich].value;
            } else {
                freqoffset = 0;
            }
            res = lms7002m_xxtsp_cmix(&d->lmsstate, LMS_TXTSP, freqoffset);
            if (res)
                return res;
        }

        d->tx_run[0] = txafen_a;
        d->tx_run[1] = txafen_b;

        res = lms7002m_mac_set(&d->lmsstate, tx_chs);
        if (res)
            return res;
        res = (d->tx_rfic_path == XSDR_TX_AUTO) ?
                  _lms7002m_signal_event(d, XSDR_TX_LNA_CHANGED) :
                  _lms7002m_set_lna_tx(d, d->tx_cfg_path);
        if (res)
            return res;
    }

    lms7002m_limelight_conf_t nlml_mode = d->lml_mode;
    if (rx_flags & RFIC_DIGITAL_LB) {
        USDR_LOG("XDEV", USDR_LOG_INFO, "%s: Enable digital loopback\n", devstr);
        nlml_mode.rx_tx_dig_loopback = 1;
    }
    if (rx_flags & RFIC_LFSR) {
        USDR_LOG("XDEV", USDR_LOG_INFO, "%s: Enable RX LFSR\n", devstr);
        nlml_mode.rx_lfsr = 1;
    }

    if (memcmp(&nlml_mode, &d->lml_mode, sizeof(nlml_mode)) != 0) {
        res = lms7002m_limelight_configure(&d->lmsstate, nlml_mode);
        if (res)
            return res;

        d->lml_mode = nlml_mode;
    }

    res = lms7002m_dc_corr_en(&d->lmsstate, d->rx_run[0], d->rx_run[1], d->tx_run[0], d->tx_run[1]);
    if (res)
        return res;
    //res = lms7_dc_init(&d->lmsstate, d->rx_run[0], d->rx_run[1], d->tx_run[0], d->tx_run[1]);

    USDR_LOG("XDEV", USDR_LOG_INFO, "%s: configure done RUN RX:%d%d TX:%d%d\n", devstr,
             d->rx_run[1], d->rx_run[0], d->tx_run[1], d->tx_run[0]);
    return 0;
}


// Internal API
static bool _check_lime_decimation(unsigned decim)
{
    switch (decim) {
    case 1:  /* 2^0 */
    case 2:  /* 2^1 */
    case 4:  /* 2^2 */
    case 8:  /* 2^3 */
    case 16: /* 2^4 */
    case 32: /* 2^5 */
        return true;
    }
    return false;
}

enum {
    LMS7_DECIM_MAX = 32,
    LMS7_INTER_MAX = 32,
};


int lms7002m_samplerate(lms7002_dev_t *d,
                        unsigned rxrate, unsigned txrate,
                        unsigned adcclk, unsigned dacclk,
                        unsigned flags, const bool rx_port_1)
{
    //bool no_8ma = false;
    lms7002m_limelight_conf_t cfg;
    cfg.rxsisoddr = 0;
    cfg.txsisoddr = 0;
    cfg.ds_high = 0;
    cfg.rx_ext_rd_fclk = 0;
    cfg.rx_lfsr = 0;
    cfg.rx_tx_dig_loopback = 0;
    cfg.rx_port = (rx_port_1) ? 1 : 0;
    cfg.rxdiv = 0;
    cfg.txdiv = 0;


    bool opt_decim_inter = ((flags & XSDR_SR_MAXCONVRATE) == XSDR_SR_MAXCONVRATE);
    bool extended_cgen_range = ((flags & XSDR_SR_EXTENDED_CGEN) == XSDR_SR_EXTENDED_CGEN);
    bool sisoddr_rx = ((flags & XSDR_LML_SISO_DDR_RX) == XSDR_LML_SISO_DDR_RX);
    bool sisoddr_tx = ((flags & XSDR_LML_SISO_DDR_TX) == XSDR_LML_SISO_DDR_TX);
    bool extclk_rx = ((flags & XSDR_LML_EXT_FIFOCLK_RX) == XSDR_LML_EXT_FIFOCLK_RX);
    bool extclk_tx = ((flags & XSDR_LML_EXT_FIFOCLK_TX) == XSDR_LML_EXT_FIFOCLK_TX);

    const unsigned mpy_adc = 4; // Always fixed to 4
    unsigned mpy_dac = 4; // Might be 4,2,1
    unsigned rxdiv = 1;
    unsigned txdiv = 1;
    unsigned tx_host_inter = 1; // Off chip extra interpolator
    unsigned rx_host_decim = 1; // Off chip extra decimator
    unsigned tx_host_mul = 1;
    unsigned rx_host_div = 1;
    unsigned txmaster_min = mpy_dac * dacclk;
    unsigned rxmaster_min = mpy_adc * adcclk;
    unsigned cgen_rate;
    int res;

    for (unsigned citer = 0; citer < (extended_cgen_range ? 2 : 1); citer++) {
        unsigned mindecint_rx = (sisoddr_rx || extclk_rx) ? 1 : 2;
        unsigned mindecint_tx = (sisoddr_tx || extclk_tx) ? 1 : 2;
        unsigned cgen_max = extended_cgen_range && (citer == 0) ? 380e6 : 320e6;
        cgen_rate = MAX(txmaster_min, rxmaster_min);

        if (cgen_rate < 1) {
            cgen_rate = MAX(mindecint_rx * rxrate * rx_host_div * mpy_adc,
                            mindecint_tx * txrate * tx_host_mul * mpy_dac);

            USDR_LOG("XDEV", USDR_LOG_NOTE, "Initial CGEN set to %03.1f Mhz\n", cgen_rate / 1.0e6);

            // For low sample rate increase DAC/ADC due to frequency aliasing
            if ((rxrate > 1 && rxrate < 2e6) || (txrate > 1 && txrate < 2e6) || opt_decim_inter) {
                for (; cgen_rate <= cgen_max; cgen_rate *= 2) {
                    unsigned rx_ndiv = (rxrate > 1) ? ((cgen_rate * 2 / (rxrate * rx_host_div)) / mpy_adc) : 0;
                    unsigned tx_ndiv = (txrate > 1) ? ((cgen_rate * 2 / (txrate * tx_host_mul)) / mpy_dac) : 0;

                    if (rx_ndiv > LMS7_DECIM_MAX || tx_ndiv > LMS7_INTER_MAX)
                        break;

                    USDR_LOG("XDEV", USDR_LOG_NOTE, "Increase RXdiv=%2d TXdiv=%2d => CGEN %03.1f Mhz\n",
                             rx_ndiv, tx_ndiv, cgen_rate * 2 / 1.0e6);
                }
            }
        }

        if (rxrate > 1) {
            rxdiv = (cgen_rate / (rxrate * rx_host_div)) / mpy_adc;
        }
        if (txrate > 1) {
            txdiv = (cgen_rate / (txrate * tx_host_mul)) / mpy_dac;
        }

        if (rxrate > 1 && !_check_lime_decimation(rxdiv)) {
            USDR_LOG("XDEV", USDR_LOG_ERROR, "can't deliver "
                                             "decimation: %d of %.3f MHz CGEN and %.3f MHz samplerate; TXm = %.3f RXm = %.3f\n",
                     rxdiv, cgen_rate / 1e6, rxrate / 1e6,
                     txmaster_min / 1e6, rxmaster_min / 1e6);
            return -EINVAL;
        }

        if (txrate > 1 && !_check_lime_decimation(txdiv)) {
            USDR_LOG("XDEV", USDR_LOG_ERROR, "can't deliver "
                                             "interpolation: %d of %.3f MHz CGEN and %.3f MHz samplerate; TXm = %.3f RXm = %.3f\n",
                     txdiv, cgen_rate / 1e6, txrate / 1e6,
                     txmaster_min / 1e6, rxmaster_min / 1e6);
            return -EINVAL;
        }

        // Store all data for correct NCO calculations
        d->rxcgen_div = mpy_adc;
        d->txcgen_div = mpy_dac;
        d->rxtsp_div = rxdiv;
        d->txtsp_div = txdiv;
        d->tx_host_inter = tx_host_inter;
        d->rx_host_decim = rx_host_decim;

        for (unsigned j = 0; j < 4; j++) {
            unsigned clkdiv = (mpy_dac == 1) ? 0 :
                              (mpy_dac == 2) ? 1 :
                              (mpy_dac == 4) ? 2 : 3;
            res = lms7002m_cgen_tune(&d->lmsstate,
                                     d->fref,
                                     cgen_rate,
                                     clkdiv);
            if (res == 0) {
                break;
            }
        }
        if (res == 0) {
            break;
        }
    }
    if (res != 0) {
        USDR_LOG("XDEV", USDR_LOG_ERROR, "can't tune VCO for data clock\n");
        return -ERANGE;
    }
    d->cgen_clk = cgen_rate;

    unsigned rxtsp_div = 1;
    if (rxrate > 0) {
        rxtsp_div = (sisoddr_rx /*|| extclk_rx*/) ? rxdiv : (((rxdiv > 1) ? (rxdiv / 2) : 1));
    }
    unsigned txtsp_div = 1;
    if (txrate > 1) {
        txtsp_div = (sisoddr_tx /*|| extclk_tx*/) ? txdiv : (((txdiv > 1) ? (txdiv / 2) : 1));
    }

    if (((rxrate > 40e6) || (txrate > 40e6))) {
        cfg.ds_high = 1;
    }

    if (d->rx_run[0] || d->rx_run[1]) {
        res = lms7002m_mac_set(&d->lmsstate, LMS7_CH_AB);
        if (res)
            return res;

        USDR_LOG("XDEV", USDR_LOG_INFO, "Update RXTSP divider\n");
        res = lms7002m_xxtsp_int_dec(&d->lmsstate, LMS_RXTSP, _ulog(d->rxtsp_div));
        if (res)
            return res;
    }
    if (d->tx_run[0] || d->tx_run[1]) {
        res = lms7002m_mac_set(&d->lmsstate, LMS7_CH_AB);
        if (res)
            return res;

        USDR_LOG("XDEV", USDR_LOG_INFO, "Update TXTSP divider\n");
        res = lms7002m_xxtsp_int_dec(&d->lmsstate, LMS_TXTSP, _ulog(d->txtsp_div));
        if (res)
            return res;
    }

    if (rxrate > 0 && extclk_rx) {
        cfg.rx_ext_rd_fclk = 1;
    }
    cfg.txdiv = txtsp_div;
    cfg.rxdiv = rxtsp_div;
    cfg.rxsisoddr = sisoddr_rx;
    cfg.txsisoddr = sisoddr_tx;

    res = lms7002m_limelight_configure(&d->lmsstate, cfg);
    if (res)
        return res;

    // Set ADS for bypass mode
    res = lms7002m_cds_set(&d->lmsstate, rxtsp_div == 1, rxtsp_div == 1);
    if (res)
        return res;

    d->lml_mode = cfg;
    USDR_LOG("XDEV", USDR_LOG_INFO, "rxrate=%.3fMHz txrate=%.3fMHz"
                            " rxdecim=%d(h_%d) txinterp=%d(h_%d)"
                            " RX_ADC=%.3fMHz TX_DAC=%.3fMHz hintr=%d hdecim=%d CGEN=%.3fMhz"
                            " RX_TSP_div=%d TX_TSP_div=%d; SISO=%d/%d; refclk=%.3f\n",
               rxrate / 1e6, txrate / 1e6,
               rxdiv, rx_host_div, txdiv, tx_host_mul,
               cgen_rate / mpy_adc / 1e6, cgen_rate / mpy_dac / 1e6,
               tx_host_inter, rx_host_decim, cgen_rate / 1e6,
               rxtsp_div, txtsp_div, sisoddr_rx, sisoddr_tx,
               d->fref / 1e6);

    // Update BW if it's in auto mode
    for (unsigned i = 0; i < 2; i++) {
        if (rxrate > 1 && d->rx_run[i] && !d->rx_bw[i].set) {
            USDR_LOG("XDEV", USDR_LOG_INFO, "Set RX[%d] bandwidth to %.3f Mhz\n", i, rxrate / 1e6);
            res = res ? res : lms7002m_mac_set(&d->lmsstate, i == 0 ? LMS7_CH_A : LMS7_CH_B);
            res = res ? res : lms7002m_rbb_bandwidth(d, rxrate, false);
        }

        if (txrate > 1 && d->tx_run[i] && !d->tx_bw[i].set) {
            USDR_LOG("XDEV", USDR_LOG_INFO, "Set TX[%d] bandwidth to %.3f Mhz\n", i, txrate / 1e6);
            res = res ? res : lms7002m_mac_set(&d->lmsstate, i == 0 ? LMS7_CH_A : LMS7_CH_B);
            res = res ? res : lms7002m_tbb_bandwidth(d, txrate, false);

            res = res ? res : lms7002m_set_gain(d, i == 0 ? LMS7_CH_A : LMS7_CH_B,
                                                RFIC_LMS7_TX_PGA_GAIN, 13, NULL);
            res = res ? res : lms7002m_set_gain(d, i == 0 ? LMS7_CH_A : LMS7_CH_B,
                                                RFIC_LMS7_TX_PAD_GAIN, 0, NULL);
        }
    }


    return res;
}


int lms7002m_set_lmlrx_mode(lms7002_dev_t *d, unsigned mode)
{
    switch (mode) {
    case XSDR_LMLRX_LFSR: d->lml_mode.rx_lfsr = 1; break;
    case XSDR_LMLRX_DIGLOOPBACK: d->lml_mode.rx_tx_dig_loopback = 1; break;
    default:
        d->lml_mode.rx_lfsr = 0;
        d->lml_mode.rx_tx_dig_loopback = 0;
        break;
    }

    return lms7002m_limelight_configure(&d->lmsstate, d->lml_mode);
}


int lms7002m_set_corr_param(lms7002_dev_t* d, int channel, int corr_type, int value)
{
    unsigned param_dc = 0;
    unsigned type = (corr_type & 0xff);
    unsigned ig, qg;
    bool rx = (corr_type >> CORR_DIR_OFF) == CORR_DIR_RX;

    int res = lms7002m_mac_set(&d->lmsstate, channel == 0 ? LMS7_CH_A : LMS7_CH_B);
    if (res)
        return res;

    if (channel == 1)
        param_dc += P_TXB_I;

    if (rx)
        param_dc += P_RXA_I;

    if (type == CORR_PARAM_Q)
        param_dc += P_TXA_Q;

    switch (type) {
    case CORR_PARAM_I:
    case CORR_PARAM_Q:
        USDR_LOG("LMS7", USDR_LOG_INFO, "Set %s%s%s to %d\n",
                 rx ? "RX" : "TX",
                 (channel == 0) ? "A" : "B",
                 type == CORR_PARAM_I ? "I" : "Q",
                 value);
        return lms7002m_dc_corr(&d->lmsstate, param_dc, value);

    case CORR_PARAM_A:
        USDR_LOG("LMS7", USDR_LOG_INFO, "Set %sA to %d\n", rx ? "RX" : "TX", value);
        return lms7002m_xxtsp_iq_phcorr(&d->lmsstate, rx ? LMS_RXTSP : LMS_TXTSP, value);

    case CORR_PARAM_GIQ:
        if (value < 0) {
            qg = 2047 + value; //Value is negative
            ig = 2047;
        } else {
            qg = 2047;
            ig = 2047 - value;
        }
        USDR_LOG("LMS7", USDR_LOG_INFO, "Set %sGIQ to %d => ig = %d qg = %d\n",
                 rx ? "RX" : "TX", value, ig, qg);
        return lms7002m_xxtsp_iq_gcorr(&d->lmsstate, rx ? LMS_RXTSP : LMS_TXTSP, ig, qg);

    case CORR_OP_SET_FREQ:
        // TODO: optimize for TDD

        res = lms7002m_sxx_tune(&d->lmsstate, rx ? SXX_TX : SXX_RX, d->fref, (unsigned)value, false);
        res = (res) ? res
                                       : lms7002m_mac_set(&d->lmsstate, channel == 0 ? LMS7_CH_A : LMS7_CH_B);
                           return res;
    case CORR_OP_SET_BW:
        if (rx) {
            return lms7002m_rbb_bandwidth(d, (unsigned)value, false);
        } else {
            return lms7002m_tbb_bandwidth(d, (unsigned)value, false);
        }

    default:
        return -EINVAL;
    }

    return 0;
}


int lms7002m_set_tx_testsig(lms7002_dev_t* d, int channel, int32_t freqoffset, unsigned pwr)
{
    unsigned scaling = d->txtsp_div / 2;
    int res;

    res = lms7002m_mac_set(&d->lmsstate, channel == 0 ? LMS7_CH_A : LMS7_CH_B);
    if (res)
        return res;

    res = lms7002m_xxtsp_gen(&d->lmsstate, LMS_TXTSP,
                             (pwr == UINT_MAX) ?  XXTSP_NORMAL: XXTSP_DC,
                             pwr & 0x7fff, pwr & 0x7fff);
    if (res)
        return res;

    res = lms7002m_xxtsp_cmix(&d->lmsstate, LMS_TXTSP, freqoffset / scaling);
    if (res)
        return res;

    return 0;
}

