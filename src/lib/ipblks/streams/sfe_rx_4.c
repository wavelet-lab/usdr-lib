// Copyright (c) 2023-2025 Wavelet Lab
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

enum exfe_rx_cmd_regs {
    EXFE_CMD_BURST_SAMPLES  = 0x00,
    EXFE_CMD_BURST_BYTES    = 0x01,
    EXFE_CMD_BURST_CAPACITY = 0x02,
    EXFE_CMD_COMPACTER      = 0x03,
    EXFE_CMD_PACKER         = 0x04,
    EXFE_CMD_ENABLE_EXT     = 0x08,

    EXFE_CMD_EXPAND         = 0x0d,
    EXFE_CMD_TXFMT12        = 0x0e,
    EXFE_CMD_MUTE           = 0x0f,

    EXFE_CMD_SHUFFLE_0      = 0x10,
    EXFE_CMD_SHUFFLE_1      = 0x11,
    EXFE_CMD_SHUFFLE_2      = 0x12,
    EXFE_CMD_SHUFFLE_3      = 0x13,
    EXFE_CMD_SHUFFLE_4      = 0x14,
    EXFE_CMD_SHUFFLE_5      = 0x15,

    EXFE_CMD_BURST_THROTTLE = 0x20,
    EXFE_CMD_RESET          = 0x30,
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
    MAX_BURSTS_RX_IN_BUFF = 32,

    // For EX TX
    MAX_BURSTS_TX_OUT_BUFF = 8,
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

struct rfe_config {
    unsigned cfg_max_bursts;
    unsigned cfg_fifo_ram_bytes;
    unsigned cfg_data_lanes_bytes;  // FE bus width in bytes
    unsigned cfg_data_lanes;        // Number of non-multiplexed data lines

    unsigned limit_samples_mod;     // Samples per burst sould be number of this modulo
    unsigned limit_burst_words;     // Maximum number of words in one burst
    unsigned limit_burst_samples;   // Maximum number of samples in one burst
};
typedef struct rfe_config rfe_config_t;

struct rfe_burster_data {
    unsigned bytes;     // Bytes per burst
    unsigned capacity;  // FIFO capacity
    unsigned count;     // Burst count
    unsigned samples;   // Samples per burst
};
typedef struct rfe_burster_data rfe_burster_data_t;

static int burst_fe_calculate(const rfe_config_t* cfg, const struct stream_config* psc, unsigned chans_raw, unsigned ch_bits, rfe_burster_data_t* out)
{
    if (psc->burstspblk > cfg->cfg_max_bursts) {
        USDR_LOG("STRM", USDR_LOG_ERROR, "SFERX4: bursts count `%d' exeeds maximum %d!\n",
                 psc->burstspblk, cfg->cfg_max_bursts);
        return -EINVAL;
    }
    if (chans_raw > cfg->cfg_data_lanes) {
        USDR_LOG("STRM", USDR_LOG_ERROR, "SFERX4: channel count %d isn't supported!\n", chans_raw);
        return -EINVAL;
    }

    unsigned bps = chans_raw * ch_bits;
    unsigned bwords = (bps * psc->spburst + cfg->cfg_data_lanes_bytes * 8 - 1) / (cfg->cfg_data_lanes_bytes * 8);
    if (bwords == 0) {
        return -EINVAL;
    }

    unsigned bursts = psc->burstspblk;
    unsigned samplerperbursts = psc->spburst;
    unsigned fifo_capacity = cfg->cfg_fifo_ram_bytes / (bwords * cfg->cfg_data_lanes_bytes);

    if ((bursts != 0) && (bwords > cfg->limit_burst_words)) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: %d samples @%s exeeds max burst size: %d words!\n",
                 psc->spburst, psc->sfmt, cfg->limit_burst_words);
        return -EINVAL;
    }

    if (bursts == 0) {
        unsigned best_bursts = 0;
        unsigned best_extra = bwords;

        for (bursts = 1; bursts <= cfg->cfg_max_bursts; bursts++) {
            if (samplerperbursts % bursts)
                continue;

            if ((samplerperbursts / bursts) % cfg->limit_samples_mod)
                continue;

            fifo_capacity = cfg->cfg_fifo_ram_bytes / ((bwords / bursts) * cfg->cfg_data_lanes_bytes);
            if (fifo_capacity <= 1)
                continue;

            if ((samplerperbursts / bursts) > cfg->limit_burst_samples)
                continue;

            if ((bwords / bursts) > cfg->limit_burst_words)
                continue;

            // Add score based
            if ((bwords % bursts) == 0)
                break;

            unsigned extra = ((bwords + bursts - 1) / bursts) * bursts - bwords;
            if (extra < best_extra) {
                best_bursts = bursts;
                best_extra = extra;
            }
        }

        if (bursts > cfg->cfg_max_bursts) {
            if (best_bursts != 0) {
                unsigned ex_bytes = ((bwords + best_bursts - 1) / best_bursts) * cfg->cfg_data_lanes_bytes -
                                    (samplerperbursts / best_bursts) * bps / 8;

                USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: Adding stub %d bytes to transfer for %d burst configuration\n", ex_bytes, best_bursts);
                bursts = best_bursts;
            } else {
                USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: %d samples @%s can't be represented with any burst count, capacity %d (FIFO is %d)!\n",
                         psc->spburst, psc->sfmt, fifo_capacity, cfg->cfg_fifo_ram_bytes);
                return -EINVAL;
            }
        }

        bwords = ((bwords + bursts - 1) / bursts);
        samplerperbursts /= bursts;
    } else {
        //check snity
        if (bursts > cfg->cfg_max_bursts)
            return -EINVAL;
        if (samplerperbursts > cfg->limit_burst_samples)
            return -EINVAL;
        if (bwords > cfg->limit_burst_words)
            return -EINVAL;
    }

    if (samplerperbursts % cfg->limit_samples_mod) {
        USDR_LOG("STRM", USDR_LOG_ERROR, "SFERX4: Burst size should be multiple of %d, requested %d x %d!\n",
                 cfg->limit_samples_mod, samplerperbursts, bursts);
        return -EINVAL;
    }

    if (fifo_capacity > SFE_CMD_BF_BTOTAL_MASK) {
        USDR_LOG("STRM", USDR_LOG_WARNING, "SFERX4: fifo capacity exceeds fe capabilities, requested: %d!",
                 fifo_capacity);
        fifo_capacity = SFE_CMD_BF_BTOTAL_MASK;
    }

    out->bytes = bwords * cfg->cfg_data_lanes_bytes;
    out->capacity = fifo_capacity;
    out->count = bursts;
    out->samples = samplerperbursts;
    return 0;
}

static int _sfe_srx4_reg_set(const sfe_cfg_t* fe, unsigned reg, unsigned val)
{
    return lowlevel_reg_wr32(fe->dev, fe->subdev, fe->cfg_base + FE_CMD_REG_ROUTE, (reg << CMD_REG_ROUTE_OFF) | val);
}

#define MAKE_EXFE_REG(a, d) (((a) << 24) | ((d) & 0xffffff))

static int _sfe_exrx_reg_set(const sfe_cfg_t* fe, unsigned reg, unsigned val)
{
    return lowlevel_reg_wr32(fe->dev, fe->subdev, fe->cfg_base + FE_CMD_REG_ROUTE, MAKE_EXFE_REG(reg, val));
}



static int _configure_simple_fe_generic(const sfe_cfg_t* fe,
                                        const struct stream_config* psc,
                                        unsigned bps,
                                        unsigned samples_mod,
                                        unsigned fe_format,
                                        unsigned chfmt,
                                        unsigned chns,
                                        struct fifo_config* pfc)
{
    // Some constants are derived from IPBLK_PARAM_BUFFER_SIZE_ADDR
    // and any other value may break configuration
    if ((1 << IPBLK_PARAM_BUFFER_SIZE_ADDR) != fe->cfg_fifomaxbytes)
        return -EINVAL;

    int res;
    rfe_config_t cfg;
    cfg.cfg_max_bursts = MAX_BURSTS_RX_IN_BUFF;
    cfg.cfg_fifo_ram_bytes = fe->cfg_fifomaxbytes;
    cfg.cfg_data_lanes_bytes = fe->cfg_word_bytes;
    cfg.cfg_data_lanes = fe->cfg_raw_chans;
    cfg.limit_samples_mod = samples_mod;
    cfg.limit_burst_words = (1u << SFE_CMD_BF_BWORDS_WIDTH); // Replace to cfg_fifomaxbytes
    cfg.limit_burst_samples = (1u << IPBLK_PARAM_BWORDS); // Replace to cfg_fifomaxbytes
    rfe_burster_data_t data;

    res = burst_fe_calculate(&cfg, psc, chns, bps, &data);
    if (res) {
        return res;
    }

    unsigned bursts = data.count;
    unsigned bwords = data.bytes / cfg.cfg_data_lanes_bytes;
    unsigned fifo_capacity = data.capacity;
    unsigned samplerperbursts = data.samples;

    USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: Stream %s/%d configured in %d words (%d samples) X %d bursts (%d bits per sym); fifo capacity %d (FMT:%x FE:%d)\n",
             psc->sfmt, samples_mod, bwords, samplerperbursts, bursts, bps, fifo_capacity, chfmt, fe_format);

    // Put everything into reset
    res = res ? res : _sfe_srx4_reg_set(fe, FE_CMD_RESET, RX_SCMD_IDLE |
                                            (1 << SFE_CMD_RST_DSP_OFF) |
                                            (1 << SFE_CMD_RST_DDR_OFF) |
                                            (1 << SFE_CMD_RST_RXSA_OFF) |
                                            (1 << SFE_CMD_RST_BURSTER_OFF));
    res = res ? res : _sfe_srx4_reg_set(fe, FE_CMD_BURST_SAMPLES, samplerperbursts - 1);
    res = res ? res : _sfe_srx4_reg_set(fe, FE_CMD_RESET, RX_SCMD_IDLE);

    res = res ? res : _sfe_srx4_reg_set(fe, FE_CMD_BURST_FORMAT,
                                            (chfmt  << SFE_CMD_BF_CHFMT_OFF) |
                                            ((fe_format) << SFE_CMD_BF_IFMT_OFF) |
                                            ((bwords - 1)  << SFE_CMD_BF_BWORDS_OFF) |
                                            (fifo_capacity << SFE_CMD_BF_BTOTAL_OFF));

    pfc->bpb = data.bytes;
    pfc->burstspblk = bursts;
    pfc->oob_len = 0;
    pfc->oob_off = 0;
    return res;
}


int sfe_rx4_configure(const sfe_cfg_t* fe,
                      const struct stream_config* psc,
                      struct fifo_config* pfc,
                      uint64_t *pwr_ch_mask)
{
    struct bitsfmt bfmt = get_bits_fmt(psc->sfmt);
    if (strcmp((const char*)bfmt.func, &DSPFUNC_CFFT_LPWR_I16[1]) == 0) {
        unsigned fft_size = 512;
        unsigned bps = 16;
        unsigned bwords = (bps * psc->spburst + 63) / 64;

        // Check why /2 is here
        if ((psc->burstspblk != 0) && (bwords > ((1u << SFE_CMD_BF_BWORDS_WIDTH)/2))) {
            USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: FFT512 %d samples @%s exeeds max burst size!\n",
                     psc->spburst, psc->sfmt);
            return -EINVAL;
        }

        return _configure_simple_fe_generic(fe, psc, bps, fft_size, IFMT_DSP, IFMT_CH_xx10, 1, pfc);
    } else if (bfmt.bits == 0) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: RX Stream format `%s' not supported!\n",
                 psc->sfmt);
        return -EINVAL;
    }
    unsigned chns = 0;
    unsigned chfmt;

    if (bfmt.complex) {
        if (psc->chcnt == 2) {
            chfmt = IFMT_CH_3210;
            chns = 4;
        } else if (psc->chcnt == 1) {
            if (psc->channels.ch_map[0] == 0) {
                chfmt = IFMT_CH_xx10;
                chns = 2;
            } else if (psc->channels.ch_map[0] == 1) {
                chfmt = IFMT_CH_32xx;
                chns = 2;
            }
        }
    } else {
        if (psc->chcnt == 4) {
            chfmt = IFMT_CH_3210;
            chns = 4;
        } else if (psc->chcnt == 2) {
            if ((psc->channels.ch_map[0] == 0 && psc->channels.ch_map[1] == 1) ||
                (psc->channels.ch_map[0] == 1 && psc->channels.ch_map[1] == 0)) {
                chfmt = IFMT_CH_xx10;
                chns = 2;
            } else if ((psc->channels.ch_map[0] == 2 && psc->channels.ch_map[1] == 3) ||
                       (psc->channels.ch_map[0] == 3 && psc->channels.ch_map[1] == 2)) {
                chfmt = IFMT_CH_32xx;
                chns = 2;
            } else if ((psc->channels.ch_map[0] == 2 && psc->channels.ch_map[1] == 0) ||
                       (psc->channels.ch_map[0] == 0 && psc->channels.ch_map[1] == 2)) {
                chfmt = IFMT_CH_x2x0;
                chns = 2;
            }
        } else if (psc->chcnt == 1) {
            if (psc->channels.ch_map[0] == 0) {
                chfmt = IFMT_CH_xxx0; chns = 1;
            } else if (psc->channels.ch_map[0] == 1) {
                chfmt = IFMT_CH_xx1x; chns = 1;
            } else if (psc->channels.ch_map[0] == 2) {
                chfmt = IFMT_CH_x2xx; chns = 1;
            } else if (psc->channels.ch_map[0] == 3) {
                chfmt = IFMT_CH_3xxx; chns = 1;
            }
        }
    }

    if (chns == 0) {
        USDR_LOG("STRM", USDR_LOG_CRITICAL_WARNING, "SFERX4: RX channel count %d is not supported in configuration [%d, %d, %d, %d, %d, %d, %d, %d ... ]!\n",
                 psc->chcnt, psc->channels.ch_map[0], psc->channels.ch_map[1], psc->channels.ch_map[2], psc->channels.ch_map[3],
                 psc->channels.ch_map[4], psc->channels.ch_map[5], psc->channels.ch_map[6], psc->channels.ch_map[7]);
        return -EINVAL;
    }

    switch (chfmt) {
    case IFMT_CH_3210: *pwr_ch_mask = 0b1111; break;
    case IFMT_CH_xx10: *pwr_ch_mask = 0b0011; break;
    case IFMT_CH_xxx0: *pwr_ch_mask = 0b0001; break;
    case IFMT_CH_xx1x: *pwr_ch_mask = 0b0010; break;
    case IFMT_CH_x2x0: *pwr_ch_mask = 0b0101; break;
    case IFMT_CH_32xx: *pwr_ch_mask = 0b1100; break;
    case IFMT_CH_x2xx: *pwr_ch_mask = 0b0100; break;
    case IFMT_CH_3xxx: *pwr_ch_mask = 0b1000; break;
    }

    unsigned fe_format = ((bfmt.bits == 8) ? IFMT_8BIT : (bfmt.bits == 12) ? IFMT_12BIT : IFMT_16BIT);
    return _configure_simple_fe_generic(fe, psc, bfmt.bits, 1, fe_format, chfmt, chns, pfc);
}


int sfe_rx4_throttle(const sfe_cfg_t* fe, bool enable, uint8_t send, uint8_t skip)
{
    int res;

    // Put everything into reset
    res = _sfe_srx4_reg_set(fe, FE_CMD_BURST_THROTTLE, RX_SCMD_IDLE |
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


int sfe_rx4_startstop(const sfe_cfg_t* fe, bool start)
{
    int res = _sfe_srx4_reg_set(fe, FE_CMD_RESET, (start) ? RX_SCMD_START_IMM : RX_SCMD_STOP_IMM);
    if (res)
        return res;

    USDR_LOG("STRM", USDR_LOG_NOTE, "SFERX4: RX Stream configured to %s\n", (start) ? "start" : "stop");
    return 0;
}


int sfe_rf4_nco_enable(const sfe_cfg_t* fe, bool enable, unsigned iqaccum)
{
    int res = 0;
    if (enable) {
        res = (res) ? res : lowlevel_reg_wr32(fe->dev, fe->subdev, fe->cfg_base + FE_CMD_REG_CFG_CORDIC, ((iqaccum & 7) << 2) | 3);
        res = (res) ? res : lowlevel_reg_wr32(fe->dev, fe->subdev, fe->cfg_base + FE_CMD_REG_CFG_CORDIC, ((iqaccum & 7) << 2) | 1);
    } else {
        res = (res) ? res : lowlevel_reg_wr32(fe->dev, fe->subdev, fe->cfg_base + FE_CMD_REG_CFG_CORDIC, 0);
    }

    USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: NCO Active: %d\n", enable);
    return res;
}

int sfe_rf4_nco_freq(const sfe_cfg_t* fe, int32_t freq)
{
    USDR_LOG("STRM", USDR_LOG_INFO, "SFERX4: NCO FREQ Set to %d\n", freq);
    return lowlevel_reg_wr32(fe->dev, fe->subdev, fe->cfg_base + FE_CMD_REG_FREQ_CORDIC, freq);
}


#define MAX_EXLG_CHANS  4
#define MAX_EX_CHANS    (1<<MAX_EXLG_CHANS)

int exfe_rx4_configure(const sfe_cfg_t* fe, const struct stream_config* psc, struct fifo_config* pfc)
{
    struct bitsfmt bfmt = get_bits_fmt(psc->sfmt);
    unsigned bps = bfmt.bits;
    if (bps != 12 && bps != 16) {
        USDR_LOG("STRM", USDR_LOG_ERROR, "EXFERX: sample size %d isn't supported!\n", bps);
        return -EINVAL;
    }
    unsigned chns = psc->chcnt;
    if (bfmt.complex) {
        chns *= 2;
    }

    unsigned j, chlg = 0;
    for (j = 1; j <= fe->cfg_raw_chans; j <<= 1, chlg++) {
        if (chns == j)
            break;
    }
    if (j > fe->cfg_raw_chans || j == 0) {
        USDR_LOG("STRM", USDR_LOG_ERROR, "EXFERX: Unsupported channel count %d!\n", chns);
        return -EINVAL;
    }
    if (fe->cfg_raw_chans >= MAX_EX_CHANS) {
        USDR_LOG("STRM", USDR_LOG_ERROR, "EXFERX: Maximum channel count supported by the core is 16, requested %d!", fe->cfg_raw_chans);
        return -EINVAL;
    }

    int res;
    rfe_config_t cfg;
    cfg.cfg_max_bursts = MAX_BURSTS_RX_IN_BUFF;
    cfg.cfg_fifo_ram_bytes = fe->cfg_fifomaxbytes;
    cfg.cfg_data_lanes_bytes = fe->cfg_word_bytes;
    cfg.cfg_data_lanes = fe->cfg_raw_chans;
    cfg.limit_samples_mod = 1;
    cfg.limit_burst_words = fe->cfg_fifomaxbytes / fe->cfg_word_bytes;
    cfg.limit_burst_samples = fe->cfg_fifomaxbytes;
    rfe_burster_data_t data;

    res = burst_fe_calculate(&cfg, psc, chns, bps, &data);
    if (res) {
        return res;
    }

    unsigned bursts = data.count;
    unsigned bbytes = data.bytes;
    unsigned fifo_capacity = data.capacity;
    unsigned samplerperbursts = data.samples;
    unsigned raw_burst_sz = (bps == 12) ? (chns * samplerperbursts * 12 + 7) / 8 : chns * samplerperbursts * 2;

    USDR_LOG("STRM", USDR_LOG_INFO, "EXFERX: Stream %s configured in %d bytes (%d samples x %d chans x %d bits) X %d bursts; naked burst size %d; fifo capacity %d\n",
             psc->sfmt, bbytes, samplerperbursts, 1 << chlg, bps, bursts, raw_burst_sz, fifo_capacity);


    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_RESET, RX_SCMD_IDLE |
                                            (1 << SFE_CMD_RST_DDR_OFF) | (1 << SFE_CMD_RST_RXSA_OFF) | (1 << SFE_CMD_RST_BURSTER_OFF));
    // Samples per burst must be updated in reset state
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_BURST_SAMPLES, samplerperbursts - 1);
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_RESET, RX_SCMD_IDLE);

    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_BURST_BYTES, bbytes - 1);
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_BURST_CAPACITY, fifo_capacity);
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_COMPACTER, chlg);
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_PACKER, bps == 12 ? 1 : 0);

    res = res ? res : exfe_trx4_update_chmap(fe, false, bfmt.complex, chns, &psc->channels);

    pfc->bpb = bbytes;
    pfc->burstspblk = bursts;
    pfc->oob_len = 0;
    pfc->oob_off = 0;
    return res;
}


int exfe_trx4_update_chmap(const sfe_cfg_t* fe,
                           bool mask,
                           bool complex,
                           unsigned total_chan_num,
                           const channel_info_t* newmap)
{
    int res = 0;
    uint8_t chmap[MAX_EX_CHANS];
    uint8_t chmap_o[MAX_EX_CHANS];
    uint8_t flag_swap_iq[MAX_EX_CHANS];
    uint16_t ch_remapped[MAX_EXLG_CHANS];
    memset(ch_remapped, 0, sizeof(ch_remapped));
    memset(flag_swap_iq, 0, sizeof(flag_swap_iq));

    unsigned lg_chans = (fe->cfg_raw_chans == 16) ? 4 : (fe->cfg_raw_chans == 8) ? 3 : (fe->cfg_raw_chans == 4) ? 2 : (fe->cfg_raw_chans == 2) ? 1 : 0;
    unsigned msk = mask ? ((complex ? total_chan_num / 2 : total_chan_num) - 1) : 0xff;

    for (unsigned g = 0; g < fe->cfg_raw_chans; g = g + total_chan_num) {
        for (unsigned f = 0; f < total_chan_num; f++) {
            unsigned swp_msk = (g + f);

            if (complex) {
                unsigned swap_iq = (newmap->ch_map[f / 2] & CH_SWAP_IQ_FLAG) ? 1 : 0;
                flag_swap_iq[g + f] = swap_iq;
                chmap_o[g + f] = (2 * (newmap->ch_map[f / 2] & msk) + ((f % 2) ^ swap_iq));
            } else  {
                chmap_o[g + f] = (newmap->ch_map[f] & msk);
            }
            chmap[g + f] = chmap_o[g + f]  ^ (swp_msk);
        }
    }

    for (unsigned g = 0; g < fe->cfg_raw_chans; g++) {
        for (unsigned f = 0; f < lg_chans; f = f + 1) {

            if (chmap[g] & (1u << f)) {
                ch_remapped[f] |= (1u << g);
            }
        }
    }

    for (unsigned g = 0; g < fe->cfg_raw_chans; g++) {
        USDR_LOG("STRM", USDR_LOG_INFO, "EXFE: CH%d => %d (orig %d MSK %x/%d) %s", g, chmap[g], chmap_o[g], msk, total_chan_num, flag_swap_iq[g] ? "SWAP_IQ" : "");
    }
    for (unsigned f = 0; f < lg_chans; f++) {
        USDR_LOG("STRM", USDR_LOG_INFO, "EXFE: STAGE_%d: %04x", f, ch_remapped[f]);
    }

    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_SHUFFLE_0, ch_remapped[0]);
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_SHUFFLE_1, ch_remapped[1]);
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_SHUFFLE_2, ch_remapped[2]);
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_SHUFFLE_3, ch_remapped[3]);
    return res;
}

int exfe_tx4_config(const sfe_cfg_t* fe, unsigned bmt, unsigned expand)
{
    int res = 0;
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_EXPAND, expand);
    res = res ? res : _sfe_exrx_reg_set(fe, EXFE_CMD_TXFMT12, bmt == 12 ? 1 : 0);
    return res;
}

int exfe_tx4_mute(const sfe_cfg_t *fe, uint64_t mutemask)
{
    return _sfe_exrx_reg_set(fe, EXFE_CMD_MUTE, mutemask);
}
