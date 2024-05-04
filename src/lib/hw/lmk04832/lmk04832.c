// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <inttypes.h>
#include "lmk04832.h"
#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>
//#include <lmk04832_defs.h>
#include <def_lmk04832.h>

enum {
    REG_ID_DEVICE_TYPE = DEVICE_TYPE,
    REG_ID_PROD_HI = PROD_HI,
    REG_ID_PROD_LOW = PROD_LOW,
    REG_ID_MASKREV = MASKREV,
    REG_ID_VNDR_HI = VNDR_HI,
    REG_ID_VNDR_LOW = VNDR_LOW,
};

#define MAKE_LMK04832_RDREG(addr) \
    ((((addr) & 0x7fff) | 0x8000) << 8)


static int lmk04832_read16(lldev_t dev, subdev_t subdev, lsopaddr_t addr, unsigned lmkaddr, unsigned* out)
{
    int res;
    unsigned v0, v1;
    res = lowlevel_spi_tr32(dev, subdev, addr,
                            MAKE_LMK04832_RDREG(lmkaddr), &v0);
    if (res)
        return res;

    res = lowlevel_spi_tr32(dev, subdev, addr,
                            MAKE_LMK04832_RDREG(lmkaddr + 1), &v1);
    if (res)
        return res;

    *out = ((v1 & 0xff) << 8) | (v0 & 0xff);
    return 0;
}

int lmk04832_reset_and_test(lldev_t dev, subdev_t subdev, lsopaddr_t addr)
{
    int res;
    unsigned prod, vndr;

    res = lowlevel_spi_tr32(dev, subdev, addr,
                            MAKE_LMK04832_RESET(1, 1), NULL);
    if (res)
        return res;


    res = lowlevel_spi_tr32(dev, subdev, addr,
                            MAKE_LMK04832_RESET_CFG(RESET_MUX_SPI_READBACK, RESET_TYPE_OUTPUT_PUSH_PULL), NULL);
    if (res)
        return res;

    res = lowlevel_spi_tr32(dev, subdev, addr,
                            MAKE_LMK04832_RESET(0, 1), NULL);
    if (res)
        return res;



    res = lmk04832_read16(dev, subdev, addr, REG_ID_PROD_HI, &prod);
    if (res)
        return res;

    res = lmk04832_read16(dev, subdev, addr, REG_ID_VNDR_HI, &vndr);
    if (res)
        return res;

    USDR_LOG("L048", USDR_LOG_INFO, "LMK PROD=%04x VNDR=%04x\n", prod, vndr);
    if (prod != 0xd163 || vndr != 0x0451)
        return -ENODEV;

    return 0;
}

enum lmk04832_limits {
    CLKINX_MOS_PLL1_MAX = 250000000u,
    CLKINX_BI_PLL1_MAX = 750000000u,
    CLKINX_BI_PLL2_MAX = 500000000u,
    CLKIN1_BI_BYPASS_MAX = 3250000000u,

    OSCIN_BI_PLL2_MAX = 500000000u,

    PLL1_PD_FREQ_MAX = 40000000,
    PLL2_PD_FREQ_MAX = 320000000,

    VCO0_MIN = 2440000000u,
    VCO0_MAX = 2580000000u,
    VCO1_MIN = 2945000000u,
    VCO1_MAX = 3255000000u,

    OUTPUT_CMOS_MAX = 250000000,

    PLL2_PRESCALER_MIN = 2,
    PLL2_PRESCALER_MAX = 8,
    PLL2_N_MAX = 262143,
    PLL2_R_MAX = 4095,
};

enum lmk_inpath {
    CLKIN0 = CLKIN_SEL_MANUAL_CLKIN0,
    CLKIN1 = CLKIN_SEL_MANUAL_CLKIN1,
    CLKIN2 = CLKIN_SEL_MANUAL_CLKIN2,
    OSCIN = CLKIN_SEL_MANUAL_HOLDOVER,
};

int lmk04832_configure_layout(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                              const lmk04832_layout_t* pcfg, lmk04832_layout_out_t *outp)
{
    int res;
    unsigned inpath; // 0 - CLK0, 1 - CLK1, 2 - CLK2, 3 - OSCIN
    bool pll1_en = false, pll2_en = false;
    unsigned pll2_r = 2;
    unsigned pll2_n = 1023;
    unsigned pll2_prescaler = 2;
    unsigned vco_freq;
    unsigned clkin0_demux = CLKIN0_DEMUX_PD;
    unsigned clkin1_demux = CLKIN1_DEMUX_PD;
    unsigned pll1_nclk_mux = PLL1_NCLK_MUX_OSCIN;
    unsigned pll2_rclk_mux = PLL2_RCLK_MUX_CLKIN;
    unsigned pll2_nclk_mux = PLL2_NCLK_MUX_PLL2_PRESCALER;
    unsigned vco_mux = VCO_MUX_CLKIN1;
    unsigned oscin_freq = 0;

    switch (pcfg->clk_route) {
    case LMK04832_CLKPATH_CLK1IN:
        inpath = CLKIN1;
        clkin1_demux = CLKIN1_DEMUX_FIN;
        break;
    case LMK04832_CLKPATH_OSCIN_PLL2:
        inpath = OSCIN;
        pll2_en = true;
        pll2_rclk_mux = PLL2_RCLK_MUX_OSCIN;
        break;
    case LMK04832_CLKPATH_CLK0IN_PLL2:
        inpath = CLKIN0;
        pll2_en = true;
        clkin0_demux = CLKIN0_DEMUX_PLL1;
        break;
    case LMK04832_CLKPATH_CLK1IN_PLL2:
        inpath = CLKIN1;
        pll2_en = true;
        clkin1_demux = CLKIN1_DEMUX_PLL1;
        break;
    case LMK04832_CLKPATH_CLK2IN_PLL2:
        inpath = CLKIN2;
        pll2_en = true;
        break;
    default:
        return -EINVAL;
    }

    if (pll1_en) {
        return -EOPNOTSUPP;
    }

    if (pll2_en) {
        if (pcfg->distribution_frequency == 0 || pcfg->distribution_frequency > VCO1_MAX)
            return -EINVAL;

        // Select VCO0 or VCO1
        const static unsigned vco_min[] = { VCO0_MIN, VCO1_MIN };
        const static unsigned vco_max[] = { VCO0_MAX, VCO1_MAX };
        unsigned k_min[2];
        unsigned k_max[2];
        bool vco_valid[2] = { false, false };

        for (unsigned j = 0; j < SIZEOF_ARRAY(vco_min); j++) {
            k_min[j] = (vco_min[j] + pcfg->distribution_frequency - 1) / pcfg->distribution_frequency;
            k_max[j] = (vco_max[j]) / pcfg->distribution_frequency;
            if (k_min[j] == 0)
                k_min[j] = 1;

            if (k_min[j] > k_max[j]) {
                continue;
            }
            vco_valid[j] = true;
        }

        USDR_LOG("L048", USDR_LOG_TRACE, "VCOs=%d%d [%u;%u] / [%u;%u]\n",
                 vco_valid[0], vco_valid[1], k_min[0], k_max[0], k_min[1], k_max[1]);
        if (!vco_valid[0] && !vco_valid[1]) {
            USDR_LOG("L048", USDR_LOG_WARNING, "Frequency %u can't be delivered by any internal VCO\n",
                     pcfg->distribution_frequency);
            return  -ERANGE;
        }
        // PLL2 N Prescaler from 2 to 8
        // PLL2 PFD up to 320Mhz
        // Fin
        // Fpfd = Fin / R
        // Fpfd = VCO / PRE / N = Fout * k / PRE / N
        // Fin / R = Fout * k / PRE /N   => Fin = Fout * k * R / PRE / N
        // Fout / Fin = PRE * N / R / k
        // (Fout / Fin) * k * R = PRE * N => Must be integer

        if (pcfg->in_frequency == 0) {
            for (unsigned r = 1; r < 255; r++) {
                for (unsigned j = 0; j < SIZEOF_ARRAY(vco_min); j++) {
                    if (!vco_valid[j])
                        continue;

                    for (unsigned k = k_min[j]; k <= k_max[j]; k++) {
                        uint64_t val = pcfg->distribution_frequency * r * k;
                        for (unsigned p = PLL2_PRESCALER_MIN; p <= PLL2_PRESCALER_MAX; p++) {
                            if (val % p)
                                continue;

                            unsigned start_n = (val / p) / PLL2_PD_FREQ_MAX;

                            USDR_LOG("L048", USDR_LOG_TRACE, "R=%u VCO=%u VAL=%" PRIu64 " p=%u SN=%u \n",
                                     r, j, val, p, start_n);

                            for (unsigned n = start_n; n < 1024; n++) {
                                if (pcfg->distribution_frequency % n)
                                    continue;

                                pll2_n = n;
                                pll2_prescaler = p;
                                pll2_r = r;
                                vco_mux = (j == 0) ? VCO_MUX_VCO0 : VCO_MUX_VCO1;
                                vco_freq = k * pcfg->distribution_frequency;
                                if (vco_freq < vco_min[j])
                                    continue;

                                if (vco_freq / pll2_prescaler / pll2_n > PLL2_PD_FREQ_MAX)
                                    continue;

                                goto pll2_configured;
                            }
                        }
                    }
                }
            }
        } else for (unsigned r = 1; r < 512; r++) {
            if (pcfg->in_frequency / r > PLL2_PD_FREQ_MAX) {
                continue;
            }

            for (unsigned j = 0; j < SIZEOF_ARRAY(vco_min); j++) {
                if (!vco_valid[j])
                    continue;

                for (unsigned k = k_min[j]; k <= k_max[j]; k++) {
                    uint64_t val = (uint64_t)pcfg->distribution_frequency * r * k;
                    uint64_t residual = val % pcfg->in_frequency;
                    USDR_LOG("L048", USDR_LOG_TRACE, "VAL=%" PRIu64 " IN=%u RES=%" PRIu64 " r*k=%u\n",
                             val, pcfg->in_frequency, residual, r * k);

                    unsigned dmax = r * k;
                    if (residual > dmax && residual < pcfg->in_frequency - dmax) {
                        continue;
                    }
                    unsigned pre_n = (val + 2 * dmax) / pcfg->in_frequency;

                    USDR_LOG("L048", USDR_LOG_TRACE, "R=%u VCO=%u VAL=%" PRIu64 " PREN=%u \n",
                             r, j, val, pre_n);

                    for (unsigned p = PLL2_PRESCALER_MIN; p <= PLL2_PRESCALER_MAX; p++) {
                        if (pre_n % p == 0) {
                            pll2_n = pre_n / p;
                            pll2_prescaler = p;
                            pll2_r = r;
                            vco_mux = (j == 0) ? VCO_MUX_VCO0 : VCO_MUX_VCO1;
                            vco_freq = k * pcfg->distribution_frequency;

                            if (vco_freq < vco_min[j])
                                continue;
                            if (pll2_n > PLL2_N_MAX)
                                continue;

                            goto pll2_configured;
                        }
                    }
                }
            }
        }
        USDR_LOG("L048", USDR_LOG_WARNING, "Frequency %u Mhz can't be delivered, no valid R/N/PRE from %u Mhz\n",
                 pcfg->distribution_frequency, pcfg->in_frequency);
        return -EINVAL;

pll2_configured:;
        unsigned infreq = ((uint64_t)vco_freq * pll2_r / pll2_prescaler / pll2_n);
        oscin_freq = (infreq / 1000000 > 255) ? 4 : (infreq / 1000000 > 127) ? 2 : (infreq / 1000000 > 63) ? 1 : 0;
        USDR_LOG("L048", USDR_LOG_INFO, "Frequency %u configured for VCO%d %uMhz (R=%d N=%d PRE=%d)\n",
                 pcfg->distribution_frequency, vco_mux, vco_freq, pll2_r, pll2_n, pll2_prescaler);

        if (outp) {
            outp->vco_frequency = vco_freq;
            outp->in_frequency = infreq;
        }
    } else {
        if (pcfg->in_frequency != 0 && pcfg->in_frequency != pcfg->distribution_frequency) {
            USDR_LOG("L048", USDR_LOG_WARNING, "In bypass configuration in_freq should be equal out_freq! (in=%u out=%u)\n",
                     pcfg->in_frequency, pcfg->distribution_frequency);
            return -EINVAL;
        }
        if (outp) {
            outp->vco_frequency = pcfg->distribution_frequency;
            outp->in_frequency = pcfg->distribution_frequency;
        }
    }

    bool jesd_en = true;
    uint32_t config_regs[] = {
        MAKE_LMK04832_VCO_BUF_CFG(vco_mux, OSCOUT_MUX_BUFF_OSCIN, OSCOUT_FMT_POWER_DOWN),
        MAKE_LMK04832_FB_CFG(pll2_rclk_mux, pll2_nclk_mux, pll1_nclk_mux, FB_MUX_EXTERNAL, 0),
        MAKE_LMK04832_OSC_SYSREF_CFG(pll1_en ? 0 : 1u, pll2_en ? 0 : 1u, pll2_en ? 0 : 1u, inpath == OSCIN ? 0 : 1u,
                             jesd_en ? 0 : 1u, jesd_en ? 0 : 1u, jesd_en ? 0 : 1u, jesd_en ? 0 : 1u),
        MAKE_LMK04832_CLKIN_CFG(0, 0, 0, 0, 0, 0, 0, 0),
        MAKE_LMK04832_CLKIN_TYPE(0, 0, (inpath == OSCIN) ? CLKIN_SEL_MANUAL_CLKIN0 : inpath, clkin1_demux, clkin0_demux), //TODO fix holdover state

        MAKE_LMK04832_CLKIN0_R_HI(0),
        MAKE_LMK04832_CLKIN0_R_LOW(1),
        MAKE_LMK04832_CLKIN1_R_HI(0),
        MAKE_LMK04832_CLKIN1_R_LOW(1),
        MAKE_LMK04832_CLKIN2_R_HI(0),
        MAKE_LMK04832_CLKIN2_R_LOW(1),

        // If PLL2 is used, program 0x173 with PLL2_PD and PLL2_PRE_PD clear to allow PLL2 to lock after PLL2_N is programmed
        MAKE_LMK04832_PLL2_PD(pll2_en ? 0 : 1u, pll2_en ? 0 : 1u, PLL2_PD_RESERVED_PLL2_PD_RESERVED),

        MAKE_LMK04832_PLL2_R_HI(pll2_r >> 8),
        MAKE_LMK04832_PLL2_R_LOW(pll2_r),
        MAKE_LMK04832_PLL2_FUNC(pll2_prescaler, oscin_freq, 0),
        MAKE_LMK04832_PLL2_N_CAL_HI(pll2_n >> 16),
        MAKE_LMK04832_PLL2_N_CAL_MID(pll2_n >> 8),
        MAKE_LMK04832_PLL2_N_CAL_LOW(pll2_n),
        MAKE_LMK04832_PLL2_N_HI(pll2_n >> 16),
        MAKE_LMK04832_PLL2_N_MID(pll2_n >> 8),
        MAKE_LMK04832_PLL2_N_LOW(pll2_n),
        0x016959, //PLL2_DLD_EN

        //SYSREF enable in continous from data clock
        //0x013903,
        //0x013a00,
        //0x013b08,
        //0x013c00,
        //0x013d08,
        //0x013e03, //PULSE CNT 1/2/4/8
    };

    for (unsigned i = 0; i < SIZEOF_ARRAY(config_regs); i++) {
        res = lowlevel_spi_tr32(dev, subdev, addr,
                                config_regs[i], NULL);
        if (res)
            return res;
    }

    return 0;
}




int lmk04832_configure_dports(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                              unsigned portx, const lmk04832_dualport_config_t* pcfg)
{
    int res;

    unsigned divlow = pcfg->divider;
    unsigned ddelay = 8;

    bool port_pd = (pcfg->portx_fmt == LMK04832_DP_POWERDOWN) &&
            (pcfg->porty_fmt == LMK04832_DP_POWERDOWN);
    bool port_x_hs = (divlow == 1) && (
                (pcfg->portx_fmt == LMK04832_DP_CML_16MA) ||
                (pcfg->portx_fmt == LMK04832_DP_CML_24MA) ||
                (pcfg->portx_fmt == LMK04832_DP_CML_32MA));
    bool port_y_sysref = (pcfg->flags & LMK04832_DP_CLK_Y_SYSREF_EN) == LMK04832_DP_CLK_Y_SYSREF_EN;

    USDR_LOG("L048", USDR_LOG_WARNING, "Port %d_%d DIV %d SYSREF_Y=%d\n",
             2*portx, 2*portx + 1, divlow, port_y_sysref);

    uint32_t port_regs[] = {
        MAKE_LMK04832_DCLK0_1_DIV_LOW(divlow),
        MAKE_LMK04832_DCLK0_1_DDLY_LOW(ddelay),
        MAKE_LMK04832_DCLK0_1_CFG(port_pd ? 1u : 0, port_x_hs ? 1u : 0, port_x_hs ? 1u : 0, port_x_hs ? 1u : 0, ddelay >> 8, divlow >> 8),
        MAKE_LMK04832_DELAY_CTRL(CLKOUT0_SRC_MUX_DEVICE_CLOCK, port_pd ? 1u : 0, port_x_hs ? 1u : 0, port_x_hs ? 0 : 0u, 0, 0),
        MAKE_LMK04832_SCLK0_1_CFG(port_y_sysref ? CLKOUT1_SRC_MUX_SYSREF : CLKOUT1_SRC_MUX_DEVICE_CLOCK, port_y_sysref ? 0 : 1, 0, 0, 0),
        MAKE_LMK04832_SCLK0_1_ADLY(0, 0),
        MAKE_LMK04832_SCLKX_Y_DDLY(0),
        MAKE_LMK04832_CLKOUT0_1_FMT(pcfg->porty_fmt, pcfg->portx_fmt)
    };

    if (portx > LMK04832_PORT_CLK12_13)
        return -EINVAL;

    for (unsigned i = 0; i < SIZEOF_ARRAY(port_regs); i++) {
        res = lowlevel_spi_tr32(dev, subdev, addr,
                                port_regs[i] + portx * 0x800, NULL);
        if (res)
            return res;
    }

    return 0;
}



int lmk04832_check_lock(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                        bool* locked)
{
    uint32_t a, b;
    int res = lowlevel_spi_tr32(dev, subdev, addr,
                                MAKE_LMK04832_RDREG(0x183), &a);
    if (res)
        return res;
    res = lowlevel_spi_tr32(dev, subdev, addr,
                                MAKE_LMK04832_RDREG(0x183), &b);
    if (res)
        return res;

    USDR_LOG("L048", USDR_LOG_INFO, "0x183 = %02u %02u\n", a, b);
    *locked = (a & 1) == 1;
    return 0;
}

int lmk04832_sysref_div_set(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                            unsigned divider)
{
    int res;
    USDR_LOG("L048", USDR_LOG_INFO, "SYSREF DIV set to %u\n", divider);
    uint32_t port_regs[] = {
        0x014310,
        0x014390,
        0x014390,
        0x014310,
        0x0144ff,

        //SYSREF enable in continous from data clock
        0x013903,

        //DDIV
        0x013a00 | ((divider >> 8) & 0x1f),
        0x013b00 | ((divider >> 0) & 0xff),

        //DDLY
        0x013c00,
        0x013d08,
        0x013e03, //PULSE CNT 1/2/4/8
    };

    for (unsigned i = 0; i < SIZEOF_ARRAY(port_regs); i++) {
        res = lowlevel_spi_tr32(dev, subdev, addr,
                                port_regs[i], NULL);
        if (res)
            return res;
    }

    return 0;
}
