// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "sfe_rx_4.h"
#include <string.h>

enum sfe_rx_regs {
    FE_CMD_REG_ROUTE,
    FE_CMD_REG_DSP,
    FE_CMD_REG_RESERVED,
    FE_CMD_REG_EVENT,

    FE_CMD_REG_FREQ_CORDIC    = 4,
    FE_CMD_REG_CFG_CORDIC     = 5,
};



enum {
    CMD_REG_ROUTE_OFF = 28,
};

enum sfe_rx_cmd_reg_route {
    // Total number of samples in burst
    FE_CMD_BURST_SAMPLES  = 0,
    // Bit format of burst, number of bits + num channels; total number of qwords in bursts and total number of bursts in buffer
    FE_CMD_BURST_FORMAT   = 1,
    // On/Off section
    FE_CMD_BURST_THROTTLE = 2,
    // Individual block reset
    FE_CMD_RESET          = 3,
};

enum {
    RX_SCMD_IDLE      = 0,
    RX_SCMD_START_AT  = 1,
    RX_SCMD_START_IMM = 2,
    RX_SCMD_STOP_AT   = 3,
    RX_SCMD_STOP_IMM  = 4,
};

enum {
    IPBLK_PARAM_BUFFER_SIZE_ADDR = 16,

    IPBLK_PARAM_BWORDS = IPBLK_PARAM_BUFFER_SIZE_ADDR - 3, // Number of WORDS (8 bytes) in a burst
    IPBLK_PARAM_BBURSTS = IPBLK_PARAM_BUFFER_SIZE_ADDR - 6,// Maximum number of bursts fits in RAM
};

enum sfe_ifmt {
    IFMT_DSP = 0,
    IFMT_8BIT = 1,
    IFMT_12BIT = 2,
    IFMT_16BIT = 3,
};

enum sfe_chfmt {
    IFMT_CH_3210 = 0,
    IFMT_CH_xx10 = 1,
    IFMT_CH_xxx0 = 2,
    IFMT_CH_xx1x = 3,
    IFMT_CH_x2x0 = 4,
    IFMT_CH_32xx = 5,
    IFMT_CH_x2xx = 6,
    IFMT_CH_3xxx = 7,
};

enum sfe_cmd_bf {
    SFE_CMD_BF_IFMT_OFF = 0,
    SFE_CMD_BF_IFMT_WIDTH = 2,
    SFE_CMD_BF_IFMT_MASK = (1u << SFE_CMD_BF_IFMT_WIDTH) - 1,

    SFE_CMD_BF_CHFMT_OFF = SFE_CMD_BF_IFMT_OFF + SFE_CMD_BF_IFMT_WIDTH,
    SFE_CMD_BF_CHFMT_WIDTH = 3,
    SFE_CMD_BF_CHFMT_MASK = (1u << SFE_CMD_BF_CHFMT_WIDTH) - 1,

    SFE_CMD_BF_BWORDS_OFF = SFE_CMD_BF_CHFMT_OFF + SFE_CMD_BF_CHFMT_WIDTH,
    SFE_CMD_BF_BWORDS_WIDTH = IPBLK_PARAM_BWORDS,
    SFE_CMD_BF_BWORDS_MASK = (1u << SFE_CMD_BF_BWORDS_WIDTH) - 1,

    SFE_CMD_BF_BTOTAL_OFF = SFE_CMD_BF_BWORDS_OFF + SFE_CMD_BF_BWORDS_WIDTH,
    SFE_CMD_BF_BTOTAL_WIDTH = IPBLK_PARAM_BBURSTS,
    SFE_CMD_BF_BTOTAL_MASK = (1u << SFE_CMD_BF_BTOTAL_WIDTH) - 1,
};

enum sfr_cmd_bt {
    SFE_CMD_BT_SKIP_OFF = 0,
    SFE_CMD_BT_SKIP_WIDTH = 8,
    SFE_CMD_BT_SKIP_MASK = (1u << SFE_CMD_BT_SKIP_WIDTH) - 1,

    SFE_CMD_BT_SEND_OFF = SFE_CMD_BT_SKIP_WIDTH,
    SFE_CMD_BT_SEND_WIDTH = 8,
    SFE_CMD_BT_SEND_MASK = (1u << SFE_CMD_BT_SEND_WIDTH) - 1,

    SFE_CMD_BT_THEN_OFF = SFE_CMD_BT_SEND_WIDTH,
    SFE_CMD_BT_THEN_WIDTH = 1,
    SFE_CMD_BT_THEN_MASK = (1u << SFE_CMD_BT_THEN_WIDTH) - 1,
};

enum sfr_cmd_rst {
    SFE_CMD_RST_RXSC = 0,

    SFE_CMD_RST_DSP_OFF = 8,
    SFE_CMD_RST_DDR_OFF = 13,
    SFE_CMD_RST_RXSA_OFF = 14,
    SFE_CMD_RST_BURSTER_OFF = 15,

    SFE_CMD_THRT_BURST_NUM_OFF = 8,
    SFE_CMD_THRT_SKIP_NUM_OFF = 0,
    SFE_CMD_THRT_ENABLE_OFF = 16,
};

enum {
    MAX_BURSTS_IN_BUFF = 32,
};


#define DSPFUNC_CFFT_LPWR_I16    "cfftlpwri16"

int sfe_rx4_check_format(const struct stream_config* psc)
{
    struct bitsfmt bfmt = get_bits_fmt(psc->sfmt);
    if (bfmt.bits != 0)
        return 0;

    // TODO check device capabilities
    if (strcmp((const char*)bfmt.func, &DSPFUNC_CFFT_LPWR_I16[1]) == 0)
        return 0;

    return -EINVAL;
}

static int _configure_cfftlpwri16(lldev_t dev,
                                  subdev_t subdev,
                                  unsigned cfg_base,
                                  unsigned cfg_fifomaxbytes,
                                  const struct stream_config* psc,
                                  struct fifo_config* pfc)
{
    int res;
    if (psc->chmsk != 1)
        return -EINVAL;

    unsigned fft_size = 512; // Assume FFT512 as a default frame size
    unsigned bps = 16;

    if (psc->spburst % fft_size) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: For this DSP function burst size should be multiple of %d!\n",
                 fft_size);
    }

    unsigned bwords = (bps * psc->spburst + 63) / 64;
    if (bwords == 0) {
        return -EINVAL;
    }

    unsigned bursts = psc->burstspblk;
    if (bursts != 0 && bursts != 1)
        return -EINVAL;

    unsigned samplerperbursts = psc->spburst;
    unsigned fifo_capacity = cfg_fifomaxbytes / (bwords * 8);

    if ((bursts != 0) && (bwords > ((1u << SFE_CMD_BF_BWORDS_WIDTH)/2))) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: %d samples @%s exeeds max burst size!\n",
                 psc->spburst, psc->sfmt);
        return -EINVAL;
    }

    bursts = 1;
    USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: Stream FFT%d configured in %d words (%d samples) X %d bursts (%d bits per sym); fifo capacity %d (FMT:%x)\n",
             fft_size, bwords, samplerperbursts, bursts, bps, fifo_capacity, 1);

    // Put everything into reset
    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_RESET << CMD_REG_ROUTE_OFF) | RX_SCMD_IDLE |
                            (1 << SFE_CMD_RST_DSP_OFF) |
                            (1 << SFE_CMD_RST_DDR_OFF) |
                            (1 << SFE_CMD_RST_RXSA_OFF) |
                            (1 << SFE_CMD_RST_BURSTER_OFF));
    if (res)
        return res;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_BURST_SAMPLES << CMD_REG_ROUTE_OFF) | (samplerperbursts - 1));
    if (res)
        return res;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_RESET << CMD_REG_ROUTE_OFF) | RX_SCMD_IDLE );
    if (res)
        return res;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_BURST_FORMAT << CMD_REG_ROUTE_OFF) |
                            (IFMT_CH_xx10  << SFE_CMD_BF_CHFMT_OFF) |
                            ((IFMT_DSP) << SFE_CMD_BF_IFMT_OFF) |
                            ((bwords - 1)  << SFE_CMD_BF_BWORDS_OFF) |
                            (fifo_capacity << SFE_CMD_BF_BTOTAL_OFF));
    if (res)
        return res;

    pfc->bpb = bwords * 8;
    pfc->burstspblk = bursts;
    pfc->oob_len = 0;
    pfc->oob_off = 0;
    return 0;
}

int sfe_rx4_configure(lldev_t dev,
                      subdev_t subdev,
                      unsigned cfg_base,
                      unsigned cfg_fifomaxbytes,
                      const struct stream_config* psc,
                      struct fifo_config* pfc)
{
    struct bitsfmt bfmt = get_bits_fmt(psc->sfmt);
    int res;
    if (strcmp((const char*)bfmt.func, &DSPFUNC_CFFT_LPWR_I16[1]) == 0) {
        return _configure_cfftlpwri16(dev, subdev, cfg_base, cfg_fifomaxbytes, psc, pfc);
    } else if (bfmt.bits == 0) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: RX Stream format `%s' not supported!\n",
                 psc->sfmt);
        return -EINVAL;
    }
    unsigned chns = 0;
    unsigned chfmt;

    if (bfmt.complex) {
        switch (psc->chmsk) {
        case 0x3: chfmt = IFMT_CH_3210; chns = 4; break;
        case 0x1: chfmt = IFMT_CH_xx10; chns = 2; break;
        case 0x2: chfmt = IFMT_CH_32xx; chns = 2; break;
        default: chns = 0;
        }
    } else {
        switch (psc->chmsk) {
        case 0xf: chfmt = IFMT_CH_3210; chns = 4; break;
        case 0x3: chfmt = IFMT_CH_xx10; chns = 2; break;
        case 0xc: chfmt = IFMT_CH_32xx; chns = 2; break;
        case 0x5: chfmt = IFMT_CH_x2x0; chns = 2; break;
        case 0x1: chfmt = IFMT_CH_xxx0; chns = 1; break;
        case 0x2: chfmt = IFMT_CH_xx1x; chns = 1; break;
        case 0x4: chfmt = IFMT_CH_x2xx; chns = 1; break;
        case 0x8: chfmt = IFMT_CH_3xxx; chns = 1; break;
        default: chns = 0;
        }
    }

    if (chns == 0) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: RX channel mask `%x' is not supported!\n",
                 psc->chmsk);
        return -EINVAL;
    }

    if (psc->burstspblk > MAX_BURSTS_IN_BUFF) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: bursts count `%d' exeeds maximum %d!\n",
                 psc->burstspblk, MAX_BURSTS_IN_BUFF);
        return -EINVAL;
    }

    unsigned bps = chns * bfmt.bits;
    unsigned bwords = (bps * psc->spburst + 63) / 64;
    if (bwords == 0) {
        return -EINVAL;
    }

    unsigned bursts = psc->burstspblk;
    unsigned samplerperbursts = psc->spburst;
    unsigned fifo_capacity = cfg_fifomaxbytes / (bwords * 8);

    if ((bursts != 0) && (bwords > (1u << SFE_CMD_BF_BWORDS_WIDTH))) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: %d samples @%s exeeds max burst size!\n",
                 psc->spburst, psc->sfmt);
        return -EINVAL;
    }
    if (bursts == 0) {
        for (bursts = 1; bursts <= MAX_BURSTS_IN_BUFF; bursts++) {
            if (bwords % bursts)
                continue;

            if (samplerperbursts % bursts)
                continue;

            fifo_capacity = cfg_fifomaxbytes / ((bwords / bursts) * 8);
            if (fifo_capacity <= 1)
                continue;

            if ((samplerperbursts / bursts) > (1u << IPBLK_PARAM_BWORDS))
                continue;

            if ((bwords / bursts) <= (1u << SFE_CMD_BF_BWORDS_WIDTH))
                break;

            //fifo_capacity = cfg_fifomaxbytes / ((bwords / bursts) * 8);
            //if (fifo_capacity == 0)
            //    continue;
        }

        if (bursts > MAX_BURSTS_IN_BUFF) {
            USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: %d samples @%s can't be represented with any burst count, capacity %d (FIFO is %d)!\n",
                     psc->spburst, psc->sfmt, fifo_capacity, cfg_fifomaxbytes);
            return -EINVAL;
        }

        bwords /= bursts;
        samplerperbursts /= bursts;
    } else {
        //check snity
        if (bursts > MAX_BURSTS_IN_BUFF)
            return -EINVAL;
        if (samplerperbursts > (1u << IPBLK_PARAM_BWORDS))
            return -EINVAL;
        if (bwords > (1u << SFE_CMD_BF_BWORDS_WIDTH))
            return -EINVAL;
    }

    if (fifo_capacity > SFE_CMD_BF_BTOTAL_MASK) {
        USDR_LOG("STRM", USDR_LOG_WARNING, "SFERX4: fifo capacity exceeds fe capabilities, requested: %d!",
                 fifo_capacity);
        fifo_capacity = SFE_CMD_BF_BTOTAL_MASK;
    }

    USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: Stream configured in %d words (%d samples) X %d bursts (%d bits per sym); fifo capacity %d (FMT:%x)\n",
             bwords, samplerperbursts, bursts, bps, fifo_capacity, chfmt);

    // Put everything into reset
    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_RESET << CMD_REG_ROUTE_OFF) | RX_SCMD_IDLE |
                            (1 << SFE_CMD_RST_DSP_OFF) |
                            (1 << SFE_CMD_RST_DDR_OFF) |
                            (1 << SFE_CMD_RST_RXSA_OFF) |
                            (1 << SFE_CMD_RST_BURSTER_OFF));
    if (res)
        return res;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_BURST_SAMPLES << CMD_REG_ROUTE_OFF) | (samplerperbursts - 1));
    if (res)
        return res;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_RESET << CMD_REG_ROUTE_OFF) | RX_SCMD_IDLE );
    if (res)
        return res;

    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_BURST_FORMAT << CMD_REG_ROUTE_OFF) |
                            (chfmt << SFE_CMD_BF_CHFMT_OFF) |
                            (((bfmt.bits == 8) ? IFMT_8BIT : (bfmt.bits == 12) ? IFMT_12BIT : IFMT_16BIT) << SFE_CMD_BF_IFMT_OFF) |
                            ((bwords - 1) << SFE_CMD_BF_BWORDS_OFF) |
                            (fifo_capacity << SFE_CMD_BF_BTOTAL_OFF));
    if (res)
        return res;

    pfc->bpb = bwords * 8;
    pfc->burstspblk = bursts;
    pfc->oob_len = 0;
    pfc->oob_off = 0;
    return 0;
}

int sfe_rx4_throttle(lldev_t dev,
                     subdev_t subdev,
                     unsigned cfg_base,
                     bool enable,
                     uint8_t send,
                     uint8_t skip)
{
    int res;

    // Put everything into reset
    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_BURST_THROTTLE << CMD_REG_ROUTE_OFF) | RX_SCMD_IDLE |
                            (((uint32_t)send) << SFE_CMD_THRT_BURST_NUM_OFF) |
                            (((uint32_t)skip) << SFE_CMD_THRT_SKIP_NUM_OFF) |
                            (enable ? (1 << SFE_CMD_THRT_ENABLE_OFF) : 0 ));
    if (res)
        return res;

    if (!enable) {
        USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: burst throttling is disabled\n");
    } else {
        USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: burst throttling is enabled %d/%d\n",
                 send + 1, (unsigned)send + skip + 2);
    }
    return 0;
}


int sfe_rx4_startstop(lldev_t dev,
                      subdev_t subdev,
                      unsigned cfg_base,
                      stream_time_t time,
                      bool start)
{
    unsigned cmd;
    int res;

    if (time != 0) {
        res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_EVENT, time);
        if (res)
            return res;

        //Timed command
        cmd = (start) ? RX_SCMD_START_AT : RX_SCMD_STOP_AT;
    } else {
        cmd = (start) ? RX_SCMD_START_IMM : RX_SCMD_STOP_IMM;
    }

    //  add | (1 << 4) in order to get increasing RAMP
    res = lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_ROUTE,
                            (FE_CMD_RESET << CMD_REG_ROUTE_OFF) | cmd );
    if (res)
        return res;

    USDR_LOG("STRM", USDR_LOG_NOTE, "SFERX4: RX Stream configured to %s @%lld\n",
             (start) ? "start" : "stop", (long long)time);
    return 0;
}


int sfe_rf4_nco_enable(lldev_t dev,
                       subdev_t subdev,
                       unsigned cfg_base,
                       bool enable,
                       unsigned iqaccum)
{
    int res = 0;
    if (enable) {
        res = (res) ? res : lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_CFG_CORDIC, ((iqaccum & 7) << 2) | 3);
        res = (res) ? res : lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_CFG_CORDIC, ((iqaccum & 7) << 2) | 1);
    } else {
        res = (res) ? res : lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_CFG_CORDIC, 0);
    }

    USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: NCO Active: %d\n", enable);
    return res;
}

int sfe_rf4_nco_freq(lldev_t dev,
                     subdev_t subdev,
                     unsigned cfg_base,
                     int32_t freq)
{
    USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: NCO FREQ Set to %d\n", freq);
    return lowlevel_reg_wr32(dev, subdev, cfg_base + FE_CMD_REG_FREQ_CORDIC, freq);
}

