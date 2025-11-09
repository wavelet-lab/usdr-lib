// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <usdr_logging.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "../device.h"
#include "../device_ids.h"
#include "../device_vfs.h"
#include "../device_names.h"
#include "../device_cores.h"

#include "../ipblks/streams/stream_limesdr.h"

#include "limesdr_ctrl.h"

static
const usdr_dev_param_constant_t s_params_u3_limesdr_0[] = {
    { DNLL_SPI_COUNT, 0 },
    { DNLL_I2C_COUNT, 0 },
    { DNLL_SRX_COUNT, 1 },
    { DNLL_STX_COUNT, 1 },
    { DNLL_RFE_COUNT, 0 },
    { DNLL_TFE_COUNT, 0 },
    { DNLL_IDX_REGSP_COUNT, 0 },
    { DNLL_IRQ_COUNT, 0 },

    // data stream cores
    { "/ll/srx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_LIME_RX_STREAM) },
    { "/ll/srx/0/base",    0 },
    { "/ll/srx/0/cfg_base",0 },
    { "/ll/stx/0/core",    USDR_MAKE_COREID(USDR_CS_STREAM, USDR_LIME_TX_STREAM) },
    { "/ll/stx/0/base",    0 },
    { "/ll/stx/0/cfg_base",0 },
    { "/ll/rfe/0/core",    0 },
    { "/ll/rfe/0/base",    0 },


    { "/ll/sdr/0/rfic/0", (uintptr_t)"lms7002m" },

};


static int dev_limesdr_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

//static int dev_limesdr_sdr_tdd_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_rx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_tx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_tx_gainlb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_limesdr_sdr_rx_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_rx_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_rx_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_rx_gainlb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_limesdr_sdr_rx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_limesdr_sdr_tx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_mp_lm7_1_gps_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_mp_lm7_1_gps_sdr_tx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_mp_lm7_1_gps_debug_lms7002m_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
static int dev_mp_lm7_1_gps_debug_lms7002m_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);


static int dev_limesdr_refclk_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);

static int dev_limesdr_sdr_revision_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);
static int dev_limesdr_sdr_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

static
const usdr_dev_param_func_t s_fparams_u3_limesdr_0[] = {
    { "/dm/rate/master",        { dev_limesdr_rate_set, NULL }},
    { "/dm/rate/rxtxadcdac",    { dev_limesdr_rate_m_set, NULL }},

    { "/dm/sdr/refclk/path",    { dev_limesdr_refclk_path_set, NULL }},
    { "/dm/power/en",           { dev_limesdr_pwren_set, NULL }},

    { "/dm/sdr/0/rx/freqency",  { dev_limesdr_sdr_rx_freq_set, NULL }},
    { "/dm/sdr/0/tx/freqency",  { dev_limesdr_sdr_tx_freq_set, NULL }},
    { "/dm/sdr/0/rx/gain",      { dev_limesdr_sdr_rx_gain_set, NULL }},
    { "/dm/sdr/0/tx/gain",      { dev_limesdr_sdr_tx_gain_set, NULL }},
    { "/dm/sdr/0/tx/gain/lb",   { dev_limesdr_sdr_tx_gainlb_set, NULL }},

    { "/dm/sdr/0/rx/path",      { dev_limesdr_sdr_rx_path_set, NULL }},
    { "/dm/sdr/0/tx/path",      { dev_limesdr_sdr_tx_path_set, NULL }},

    { "/dm/sdr/0/rx/gain/pga",  { dev_limesdr_sdr_rx_gainpga_set, NULL }},
    { "/dm/sdr/0/rx/gain/vga",  { dev_limesdr_sdr_rx_gainvga_set, NULL }},
    { "/dm/sdr/0/rx/gain/lna",  { dev_limesdr_sdr_rx_gainlna_set, NULL }},
    { "/dm/sdr/0/rx/gain/lb",   { dev_limesdr_sdr_rx_gainlb_set, NULL }},

    { "/dm/sdr/0/rx/bandwidth", { dev_mp_lm7_1_gps_sdr_rx_bandwidth_set, NULL }},
    { "/dm/sdr/0/tx/bandwidth", { dev_mp_lm7_1_gps_sdr_tx_bandwidth_set, NULL }},

    // Debug interface
    { "/debug/hw/lms7002m/0/reg",  { dev_mp_lm7_1_gps_debug_lms7002m_reg_set, dev_mp_lm7_1_gps_debug_lms7002m_reg_get }},

    { "/dm/revision",           { NULL, dev_limesdr_sdr_revision_get }},
    { "/dm/debug/all",          { NULL, dev_limesdr_sdr_debug_all_get }},
};


struct dev_limesdr {
    device_t base;
    lowlevel_ops_t my_ops;
    lowlevel_ops_t* p_original_ops;

    limesdr_dev_t limedev;

    uint32_t debug_lms7002m_last;
    bool started;

    stream_handle_t* rx;
    stream_handle_t* tx;
};


int dev_limesdr_sdr_debug_all_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    // stub
    *ovalue++ = 0;
    *ovalue   = 0;

    return 0;
}


enum {
    RATE_MIN = 90000,
    RATE_MAX = 2*61440000,
};


int dev_limesdr_rate_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    if (value < RATE_MIN || value > RATE_MAX)
        return -ERANGE;

    struct dev_limesdr *d = (struct dev_limesdr *)ud;

     //Simple SISO RX only
    return limesdr_set_samplerate(&d->limedev, (unsigned)value, (unsigned)value, 0, 0);
}

int dev_limesdr_rate_m_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    uint32_t *rates = (uint32_t *)(uintptr_t)value;

    uint32_t rx_rate = rates[0];
    uint32_t tx_rate = rates[1];

    uint32_t adc_rate = rates[2];
    uint32_t dac_rate = rates[3];

    if (rx_rate == 0 && tx_rate == 0)
        return -EINVAL;

    if ((rx_rate != 0) && (rx_rate < RATE_MIN || rx_rate > RATE_MAX))
        return -ERANGE;

    if ((tx_rate != 0) && (tx_rate < RATE_MIN || tx_rate > RATE_MAX))
        return -ERANGE;

    return limesdr_set_samplerate(&d->limedev, rx_rate, tx_rate, adc_rate, dac_rate);
}

int dev_limesdr_pwren_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}
int dev_limesdr_refclk_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    //LimeSDR-mini r2 doesn't support clock source selection
    return 0;
}

int dev_limesdr_sdr_rx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_fe_set_freq(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_TUNE_RX_FDD, value, NULL);
}
int dev_limesdr_sdr_tx_freq_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_fe_set_freq(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_TUNE_TX_FDD, value, NULL);
}
int dev_limesdr_sdr_rx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_set_gain(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_RX_LNA_GAIN, value, NULL);
}
int dev_limesdr_sdr_tx_gain_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_set_gain(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_TX_PAD_GAIN, value, NULL);
}
int dev_limesdr_sdr_tx_gainlb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_set_gain(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_TX_LB_GAIN, value, NULL);
}
int dev_limesdr_sdr_rx_gainpga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_set_gain(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_RX_PGA_GAIN, value, NULL);
}
int dev_limesdr_sdr_rx_gainvga_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_set_gain(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_RX_TIA_GAIN, value, NULL);
}
int dev_limesdr_sdr_rx_gainlna_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_set_gain(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_RX_LNA_GAIN, value, NULL);
}
int dev_limesdr_sdr_rx_gainlb_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_set_gain(&d->limedev.base, LMS7_CH_AB, RFIC_LMS7_RX_LB_GAIN, value, NULL);
}

int dev_limesdr_sdr_rx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

int dev_limesdr_sdr_tx_path_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return 0;
}

int dev_mp_lm7_1_gps_sdr_rx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_bb_set_badwidth(&d->limedev.base, LMS7_CH_AB, false, value, NULL);
}
int dev_mp_lm7_1_gps_sdr_tx_bandwidth_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    return lms7002m_bb_set_badwidth(&d->limedev.base, LMS7_CH_AB, true, value, NULL);
}





int dev_mp_lm7_1_gps_debug_lms7002m_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    int res;
    unsigned chan = (unsigned)(value >> 32);

    res = lms7002m_mac_set(&d->limedev.base.lmsstate, chan);

    d->debug_lms7002m_last = ~0u;
    res = lowlevel_spi_tr32(d->base.dev, 0, 0, value & 0xffffffff, &d->debug_lms7002m_last);
    USDR_LOG("LMIN", USDR_LOG_WARNING, "%s: Debug LMS7/%d REG %08x => %08x\n",
             lowlevel_get_devname(d->base.dev), chan, (unsigned)value,
             d->debug_lms7002m_last);
    return res;
}
int dev_mp_lm7_1_gps_debug_lms7002m_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct dev_limesdr *d = (struct dev_limesdr *)ud;
    *ovalue = d->debug_lms7002m_last;
    return 0;
}

limesdr_dev_t* get_limesdr_dev(pdevice_t udev)
{
    struct dev_limesdr *d = (struct dev_limesdr *)udev;
    return &d->limedev;
}

int dev_limesdr_sdr_revision_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    //emulated
    uint32_t rev_lo = 0, rev_hi = 0;

    rev_lo |= (uint32_t)1 << 27;
    rev_lo |= (uint32_t)1 << 23;
    rev_lo |= (uint32_t)13 << 17;
    rev_lo |= (uint32_t)1 << 12;
    rev_lo |= (uint32_t)1 << 6;
    rev_lo |= (uint32_t)1;

    rev_hi |= (uint32_t)2 << 8;

    *ovalue = rev_hi;
    *ovalue = *ovalue << 32;
    *ovalue |= rev_lo;

    return 0;
}

static
int dev_limesdr_initialize(lldev_t dev, subdev_t subdev, lowlevel_stream_params_t* params, stream_t* channel)
{
    struct dev_limesdr *d = (struct dev_limesdr *)lowlevel_get_device(dev);
    int res;

    if (getenv("USDR_BARE_DEV")) {
        return -EOPNOTSUPP;
    }

    res = limesdr_prepare_streaming(&d->limedev);
    if (res) {
        return res;
    }

    res = d->p_original_ops->stream_initialize(dev, subdev, params, channel);
    return res;
}

static
int dev_limesdr_stream_deinitialize(lldev_t dev, subdev_t subdev, stream_t channel)
{
    //struct dev_limesdr *d = (struct dev_limesdr *)lowlevel_get_device(dev);
    //xsdr_rfic_streaming_down(&d->xdev, RFIC_LMS7_RX);

    return 0;
}


static
int limesdr_device_initialize(pdevice_t udev, unsigned pcount, const char** devparam, const char** devval)
{
    struct dev_limesdr *d = (struct dev_limesdr *)udev;
    lldev_t dev = d->base.dev;
    int res;

    res = limesdr_init(&d->limedev);
    if (res)
        return res;

    // Proxy operations
    memcpy(&d->my_ops, lowlevel_get_ops(dev), sizeof (lowlevel_ops_t));
    d->my_ops.stream_initialize = &dev_limesdr_initialize;
    d->my_ops.stream_deinitialize = &dev_limesdr_stream_deinitialize;
    d->p_original_ops = lowlevel_get_ops(dev);
    dev->ops = &d->my_ops;

    return 0;
}

static
int limesdr_device_create_stream(device_t* dev, const char* sid, const char* dformat,
                                        const usdr_channel_info_t* channels, unsigned pktsyms,
                                        unsigned flags, const char* parameters, stream_handle_t** out_handle)
{
    struct dev_limesdr *d = (struct dev_limesdr *)dev;
    int res = -EINVAL;
    bool rx = (strstr(sid, "rx") != 0);

    // TODO: pass channels
    res = create_limesdr_stream(dev,
                                rx ? 0 : 1, dformat, 0, pktsyms, flags,
                                out_handle);
    if (res) {
        return res;
    }

    if (rx) {
        d->rx = *out_handle;
    } else {
        d->tx = *out_handle;
    }
    return 0;
}

static
int limesdr_device_unregister_stream(device_t* dev, stream_handle_t* stream)
{
    struct dev_limesdr *d = (struct dev_limesdr *)dev;
    if (stream == d->tx) {
        d->tx = NULL;
    } else if (stream == d->rx) {
        d->rx = NULL;
    }
    //return -EINVAL; TODO!!!
    return 0;
}


static
void limesdr_device_destroy(pdevice_t udev)
{
    struct dev_limesdr *d = (struct dev_limesdr *)udev;

    limesdr_dtor(&d->limedev);
    USDR_LOG("LMIN", USDR_LOG_INFO, "LIMESDR: turnoff\n");

    usdr_device_base_destroy(udev);
}

int limesdr_device_stream_sync(device_t* device,
                               stream_handle_t** pstr, unsigned scount, const char* synctype)
{
    struct dev_limesdr *d = (struct dev_limesdr *)device;

    if (!strcmp(synctype, "off")) {
        limesdr_disable_stream(&d->limedev);
        d->started = false;
    } else {
        if (!d->started) {
            d->started = true;
            limesdr_setup_stream(&d->limedev, false, true, false);
        } else {
            USDR_LOG("LMIN", USDR_LOG_INFO, "Streaming is already enabled\n");
        }
    }

    return 0;
}

static
int limesdr_device_create(lldev_t dev, device_id_t devid)
{
    int res;
    struct dev_limesdr *d = (struct dev_limesdr *)malloc(sizeof(struct dev_limesdr));
    res = limesdr_ctor(dev, &d->limedev);
    if (res){
        goto failed_free;
    }

    res = usdr_device_base_create(&d->base, dev);
    if (res) {
        goto failed_free;
    }

    res = vfs_add_const_i64_vec(&d->base.rootfs,
                                s_params_u3_limesdr_0,
                                SIZEOF_ARRAY(s_params_u3_limesdr_0));
    if (res)
        goto failed_tree_creation;

    res = usdr_vfs_obj_param_init_array(&d->base,
                                        s_fparams_u3_limesdr_0,
                                        SIZEOF_ARRAY(s_fparams_u3_limesdr_0));
    if (res)
        goto failed_tree_creation;

    d->base.initialize = &limesdr_device_initialize;
    d->base.destroy = &limesdr_device_destroy;
    d->base.create_stream = &limesdr_device_create_stream;
    d->base.unregister_stream = &limesdr_device_unregister_stream;
    d->base.timer_op = &limesdr_device_stream_sync;
    d->started = false;
    dev->pdev = &d->base;
    return 0;

failed_tree_creation:
    usdr_device_base_destroy(&d->base);
failed_free:
    free(d);
    return res;
}

static const
struct device_factory_ops s_ops = {
    limesdr_device_create,
};

int usdr_device_register_limesdr()
{
    return usdr_device_register(U3_LIMESDR_DEVICE_ID_C, &s_ops);
}
