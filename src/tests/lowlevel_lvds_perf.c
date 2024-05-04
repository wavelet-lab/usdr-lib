// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <usdr_lowlevel.h>
#include <usdr_logging.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>

#include "../lib/hw/si549/si549.h"
#include "../lib/hw/lmk04832/lmk04832.h"
#include "../lib/hw/tps6594/tps6594.h"
#include "../lib/hw/tmp108/tmp108.h"
#include "../lib/hw/ads42lbx9/ads42lbx9.h"

static volatile int s_exit_event = 0;
void sig_term(int signo) {
    (void)signo;

    if (s_exit_event) {
        exit(1);
    }

    s_exit_event = 1;
}

enum {
    M2PCI_REG_STAT_CTRL = 0,
};

enum dev_gpi {
    IGPI_BANK_ADC_CLK_HI = 0,
    IGPI_BANK_ADC_CLK_LO = 1,

    IGPI_BANK_ADC_STAT   = 4,
    IGPI_BANK_ADC_PBRSHI = 5,
    IGPI_BANK_ADC_PBRSLO = 6,


    IGPI_BANK_REFCNTR0_0 = 8,
    IGPI_BANK_REFCNTR0_1 = 9,
    IGPI_BANK_REFCNTR0_2 = 10,
    IGPI_BANK_REFCNTR0_3 = 11,
    IGPI_BANK_REFCNTR1_0 = 12,
    IGPI_BANK_REFCNTR1_1 = 13,
    IGPI_BANK_REFCNTR1_2 = 14,
    IGPI_BANK_REFCNTR1_3 = 15,
    IGPI_BANK_ALARMCNTR_0 = 16,
    IGPI_BANK_ALARMCNTR_1 = 17,
    IGPI_BANK_ALARMCNTR_2 = 18,
    IGPI_BANK_ALARMCNTR_3 = 19,
    IGPI_BANK_DACLINKST_0 = 20,
    IGPI_BANK_DACLINKST_1 = 21,
    IGPI_BANK_DACLINKST_2 = 22,
    IGPI_BANK_DACLINKST_3 = 23,
};

enum dev_gpo {
    IGPO_BANK_CFG_RCLK    = 0,
    IGPO_BANK_CFG_DAC     = 1,
    IGPO_BANK_CFG_ADC     = 2,
    IGPO_BANK_PGA_LE      = 3,
    IGPO_BANK_PGA_GAIN    = 4,

    IGPO_BANK_ADC_CHMSK     = 5,
    IGPO_BANK_ADC_PHY_CTL_0 = 6,
    IGPO_BANK_ADC_PHY_CTL_1 = 7,
    IGPO_BANK_ADC_PHY_PBRS  = 8,
};

enum {
    PHY_LVDS_DATA_OFF = 0,
    PHY_LVDS_BITSLIP_RST_OFF = 5,
    PHY_LVDS_FCK_LD_OFF = 6,
    PHY_LVDS_CLK_LD_OFF = 7,
    PHY_LVDS_L0_LD_OFF = 8,
    PHY_LVDS_L1_LD_OFF = 9,
    PHY_LVDS_L2_LD_OFF = 10,
    PHY_LVDS_L3_LD_OFF = 11,
    PHY_LVDS_RD_OFF = 12,
};
enum {
    PHY_WR_CK = 1,
    PHY_WR_FR = 0,
    PHY_WR_L0 = 2,
    PHY_WR_L1 = 3,
    PHY_WR_L2 = 4,
    PHY_WR_L3 = 5,
};

enum {
    PHY_RD_L0 = 0,
    PHY_RD_L1 = 1,
    PHY_RD_L2 = 2,
    PHY_RD_L3 = 3,
    PHY_RD_FR = 4,
    PHY_RD_CK = 5,
};

static int dev_gpo_set(lldev_t dev, unsigned bank, unsigned data)
{
    return lowlevel_reg_wr32(dev, 0, M2PCI_REG_STAT_CTRL, ((bank & 0x7f) << 24) | (data & 0xff));
}

static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, M2PCI_REG_STAT_CTRL, 16 + (bank / 4), data);
}

int check_adc_clk(lldev_t dev, unsigned fmean[4], unsigned fdiv[4])
{
    int res;
    unsigned adc, stat;
    unsigned mask[4] = {0xdeadbeef, 0x1ee1c0de, 0xccddeeff, 0x0};
    unsigned k, m[4];
    unsigned fmeas[100][4];
    unsigned prev[4];

    memset(m, 0, sizeof(m));

    for (k = 0; (k < 50); k++) {
        for (adc = 0; adc < 4; adc++) {
            res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, 0 | (adc << 4));
            if (res)
                return res;

            res = dev_gpi_get32(dev, IGPI_BANK_ADC_CLK_HI, &stat);
            if (res)
                return res;
            uint32_t masked = (stat ^ mask[adc]);
            unsigned gen = masked >> 24;
            uint64_t freq = ((masked & 0xffffff) * 100000000ul) / (1u<<20);

            USDR_LOG("0944", USDR_LOG_NOTE, "ADC_CLK[%d] = %08d (%08x %08x)\n", adc, (unsigned)freq, stat, masked);
            if (k > 4) {
                if (prev[adc] != gen && m[adc] < 100) {
                    fmeas[m[adc]++][adc] = freq;
                }
            }
            prev[adc] = gen;

        }
        usleep(10000);
    }

    for (adc = 0; adc < 4; adc++) {
        if (m[adc] == 0) {
           fmean[adc] = 0;
           fdiv[adc] = 0;
           continue;
        }
        double mean = 0;
        for (k = 0; k < m[adc]; k++)
            mean += fmeas[k][adc];
        mean /= m[adc];

        double sdiv = 0;
        for (k = 0; k < m[adc]; k++)
            sdiv += (fmeas[k][adc] - mean) * (fmeas[k][adc] - mean);

        sdiv = sqrt(sdiv / m[adc]);

        fmean[adc] = mean;
        fdiv[adc] = sdiv;
    }

    return 0;
}

int lvds_reg_read(lldev_t dev, unsigned lvdsv, uint32_t* v)
{
    int res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_1, (lvdsv) << (PHY_LVDS_RD_OFF - 8));
    if (res)
        return res;

    res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, v);
    if (res)
        return res;

    *v = *v & 0xff;
    return 0;
}

int lvds_reg_delay_latch(lldev_t dev, unsigned lvdsv, unsigned step)
{
    int res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, (1 << PHY_LVDS_BITSLIP_RST_OFF) | (1 << (PHY_LVDS_FCK_LD_OFF + lvdsv)) | step);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, (1 << PHY_LVDS_BITSLIP_RST_OFF) | step);
    if (res)
        return res;
    return 0;
}

int lvds_reg_delay_latch_bs(lldev_t dev, unsigned lvdsv, unsigned step)
{
    int res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, (1 << (PHY_LVDS_FCK_LD_OFF + lvdsv)) | step);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_0, step);
    if (res)
        return res;
    return 0;
}

int check_adc_msk(lldev_t dev, int longtest, uint8_t gooda[32], uint8_t goodb[32])
{
    const unsigned iters = (longtest > 999) ? longtest : 999;

    int res;
    for (unsigned step = 0; step < 32; step++) {
        res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, 0xf);
        if (res)
            return res;

        res = lvds_reg_delay_latch(dev, PHY_WR_CK, step);
        if (res)
            return res;

        unsigned fk[iters][4];
        unsigned ck[iters][4];


        for (unsigned p = 0; p < 4; p++) {
            res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, (1u << p) | (p << 4));
            if (res)
                return res;

            // FR
            res = lvds_reg_read(dev, PHY_RD_FR, &fk[0][p]);
            if (res)
                return res;

            for (unsigned j = 1; !s_exit_event && (j < iters); j++) {
                res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &fk[j][p]);
                if (res)
                    return res;
                fk[j][p] = fk[j][p] & 0xff;
            }

            //CK
            res = lvds_reg_read(dev, PHY_RD_CK, &ck[0][p]);
            if (res)
                return res;

            for (unsigned j = 1; !s_exit_event && (j < iters); j++) {
                res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &ck[j][p]);
                if (res)
                    return res;
                ck[j][p] = ck[j][p] & 0xff;
            }
        }

        unsigned good_ck[] = { 0x55, 0xaa };
        unsigned good_fk[] = { 0x33, 0x66, 0xcc, 0x99 };

        unsigned bins_ck[4][sizeof(good_ck)], fail_ck[4];
        unsigned bins_fk[4][sizeof(good_fk)], fail_fk[4];

        memset(bins_ck, 0, sizeof(bins_ck));
        memset(fail_ck, 0, sizeof(fail_ck));
        memset(bins_fk, 0, sizeof(bins_fk));
        memset(fail_fk, 0, sizeof(fail_fk));


        for (unsigned g = 0; g < iters; g++) {
            for (unsigned a = 0; a < 4; a++) {
                for (unsigned q = 0; q < SIZEOF_ARRAY(good_ck); q++) {
                    if (ck[g][a] == good_ck[q]) {
                        bins_ck[a][q]++;
                        goto next_p0;
                    }
                }
                fail_ck[a]++;

                next_p0:;
                for (unsigned q = 0; q < SIZEOF_ARRAY(good_fk); q++) {
                    if (fk[g][a] == good_fk[q]) {
                        bins_fk[a][q]++;
                        goto next_p1;
                    }
                }
                fail_fk[a]++;

                next_p1:;
            }

#if 0
            fprintf(stderr, "STEP[%2d] %d: %02x %02x %02x %02x --  %02x %02x %02x %02x\n",
                    step, g,
                    fk[g][0], fk[g][1], fk[g][2], fk[g][3],
                    ck[g][0], ck[g][1], ck[g][2], ck[g][3]);
#endif
        }

        fprintf(stderr, "ADC STEP[%2d] %c [%5d] %5d %5d %5d %5d -- [%5d] %5d %5d   == %c [%5d] %5d %5d %5d %5d -- [%5d] %5d %5d\n",
                step,
                (fail_fk[0] == 0 && fail_ck[0] == 0) ? '*' : (( fail_fk[0] == 0) ? 'F' : ((fail_ck[0] == 0) ? 'C' : ' ')),
                fail_fk[0], bins_fk[0][0], bins_fk[0][1], bins_fk[0][2], bins_fk[0][3],
                fail_ck[0], bins_ck[0][0], bins_ck[0][1],
                (fail_fk[1] == 0 && fail_ck[1] == 0) ? '*' : (( fail_fk[1] == 0) ? 'F' : ((fail_ck[1] == 0) ? 'C' : ' ')),
                fail_fk[1], bins_fk[1][0], bins_fk[1][1], bins_fk[1][2], bins_fk[1][3],
                fail_ck[1], bins_ck[1][0], bins_ck[1][1]
                );

        gooda[step] = (fail_fk[0] == 0 && fail_ck[0] == 0) ? '*' : (( fail_fk[0] == 0) ? 'F' : ((fail_ck[0] == 0) ? 'C' : ' '));
        goodb[step] = (fail_fk[1] == 0 && fail_ck[1] == 0) ? '*' : (( fail_fk[1] == 0) ? 'F' : ((fail_ck[1] == 0) ? 'C' : ' '));
    }

    return 0;
}
enum {
    PBRS_CTL_START = 7,
    PBRS_CTL_STOP = 6,
    PBRS_CTL_LATCH = 5,
};

#define MIN(x,y) (((x) > (y)) ? (y) : (x))

int check_pbrs(lldev_t dev, int longtest,
               unsigned gooda[32], unsigned goodb[32], unsigned goodc[32], unsigned goodd[32])
{
    int res;
unsigned g = 0;
    //for (unsigned g = 0; g < 12; g++) {
    for (unsigned step = 0; !s_exit_event && (step < 32); step++) {
        res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, 0xf);
        if (res)
            return res;

        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 1u << PBRS_CTL_STOP);
        if (res)
            return res;
        usleep(1000);
        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 0);
        if (res)
            return res;

        // Set Delay
        res = lvds_reg_delay_latch_bs(dev, PHY_WR_CK, step);
        if (res)
            return res;

        // Run PBRS check
        usleep(10000);

        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 1u << PBRS_CTL_START);
        if (res)
            return res;
        usleep(1000);
        res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 0);
        if (res)
            return res;

        if (longtest == 0)
            usleep(100000);
        else
            usleep(longtest * 1000);

        unsigned adc_ber[4];
        unsigned adc_lst[4];


        for (unsigned a = 0; a < 4; a++) {
            uint32_t d;

            res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, (1u << a) | (a << 4));
            if (res)
                return res;

            res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 0 | (1u << PBRS_CTL_LATCH));
            if (res)
                return res;
            res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 0 | 0);
            if (res)
                return res;

            res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &d);
            if (res)
                return res;

            adc_ber[a] = ((d & 0x00ff0000) >> 16) | ((d & 0x0000ff00) >> 8);

            res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 1 | (1u << PBRS_CTL_LATCH));
            if (res)
                return res;
            res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 1 | 0);
            if (res)
                return res;

            res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &d);
            if (res)
                return res;

            adc_ber[a] |= (((d & 0x00ff0000) >> 16) | ((d & 0x0000ff00) >> 8)) << 16;

            ///////////////////////////////////////////////////////////////
            res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 2 | (1u << PBRS_CTL_LATCH));
            if (res)
                return res;
            res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 2 | 0);
            if (res)
                return res;

            res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &d);
            if (res)
                return res;

            adc_lst[a] = ((d & 0x00ff0000) >> 16) | ((d & 0x0000ff00) >> 8);

            res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 3 | (1u << PBRS_CTL_LATCH));
            if (res)
                return res;
            res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_PBRS, 3 | 0);
            if (res)
                return res;

            res = dev_gpi_get32(dev, IGPI_BANK_ADC_STAT, &d);
            if (res)
                return res;

            adc_lst[a] |= (((d & 0x00ff0000) >> 16) | ((d & 0x0000ff00) >> 8)) << 16;
        }

        if (g == 0) {
            gooda[step] = adc_ber[0] + adc_lst[0];
            goodb[step] = adc_ber[1] + adc_lst[1];
            goodc[step] = adc_ber[2] + adc_lst[2];
            goodd[step] = adc_ber[3] + adc_lst[3];
        } else {
            gooda[step] = MIN(gooda[step], adc_ber[0] + adc_lst[0]);
            goodb[step] = MIN(goodb[step], adc_ber[1] + adc_lst[1]);
            goodc[step] = MIN(goodc[step], adc_ber[2] + adc_lst[2]);
            goodd[step] = MIN(goodd[step], adc_ber[3] + adc_lst[3]);
        }

        fprintf(stderr, "ADC STEP%d[%2d]  %8d %8d %8d %8d -- %8d %8d %8d %8d\n",
                g, step,
                adc_ber[0], adc_ber[1], adc_ber[2], adc_ber[3],
                adc_lst[0], adc_lst[1], adc_lst[2], adc_lst[3]);

    }
    /*
    res = dev_gpo_set(dev, IGPO_BANK_ADC_CHMSK, 0xf);
    if (res)
        return res;
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_1, 0);
    res = dev_gpo_set(dev, IGPO_BANK_ADC_PHY_CTL_1, 0x80);

    }
*/

    return 0;
}

int main(int argc, char** argv)
{
    int res, opt;
    lldev_t dev;
    unsigned rate = 50e6;
    unsigned ratemax = 250e6;
    usdrlog_setlevel(NULL, USDR_LOG_TRACE);
    usdrlog_enablecolorize(NULL);
    unsigned iten = 0; // Iterate over smaplerate
    unsigned cont = 0; // Continous test
    unsigned longtest = 0;
    while ((opt = getopt(argc, argv, "l:w:r:R:ic:L:")) != -1) {
        switch (opt) {
        case 'L':
            longtest = atoi(optarg);
            break;
        case 'i':
            iten = 1;
            break;
        case 'l':
            usdrlog_setlevel(NULL, atoi(optarg));
            break;
        case 'r':
            rate = atof(optarg);
            break;
        case 'R':
            ratemax = atof(optarg);
            break;
        case 'c':
            cont = atoi(optarg);
            break;
        default:
            fprintf(stderr, "Usage: %s [-l loglevel] [-r rate]\n",
                    argv[0]);
            return 1;
        }
    }

    res = lowlevel_create(0, NULL, NULL, &dev, 0, NULL, 0);
    if (res) {
        fprintf(stderr, "Unable to create: errno %d\n", res);
        return 1;
    }

    fprintf(stderr, "Device was created!\n");

    res = tps6594_vout_ctrl(dev, 0, 1, TPS6594_LDO4, true);
    if (res) {
        fprintf(stderr, "Op failed: errno %d\n", res);
        return 2;
    }

    usleep(10000);
    res = dev_gpo_set(dev, IGPO_BANK_CFG_ADC, 0x03);
    if (res)
        return res;

    res = dev_gpo_set(dev, IGPO_BANK_CFG_ADC, 0x0);
    if (res)
        return res;


    signal(SIGTERM, sig_term);
    signal(SIGINT,  sig_term);

    int set = 0;
    do {
    double crate = rate;
    do {
        if (iten || set == 0) {
            res = si549_set_freq(dev, 0, 0, crate);
            if (res) {
                fprintf(stderr, "Op failed: errno %d\n", res);
                return 2;
            }
            usleep(10000);
            res = si549_enable(dev, 0, 0, true);
            if (res)
                return res;

            usleep(35000);
/*
            //Wait freq to stabilize to reset logic
            res = dev_gpo_set(dev, IGPO_BANK_CFG_ADC, 0xf0);
            if (res)
                return res;

            res = dev_gpo_set(dev, IGPO_BANK_CFG_ADC, 0x0);
            if (res)
                return res;
*/
            // Put ADS into PBRS mode
            res = ads42lbx9_set_pbrs(dev, 0, 0);
            if (res)
                return res;
            res = ads42lbx9_set_pbrs(dev, 0, 1);
            if (res)
                return res;


            set = 1;
        }

        //Check ADC CLK rates
        unsigned fmean[4];
        unsigned fdiv[4];
        uint8_t gooda[32];
        uint8_t goodb[32];
        unsigned bgooda[32];
        unsigned bgoodb[32];
        unsigned bgoodc[32];
        unsigned bgoodd[32];
        unsigned* bgood_p[] = {bgooda, bgoodb, bgoodc, bgoodd};

        //Wait freq to stabilize to reset logic
        res = dev_gpo_set(dev, IGPO_BANK_CFG_ADC, 0xf0);
        if (res)
            return res;

        res = check_adc_clk(dev, fmean, fdiv);
        if (res) {
            goto failed;
        }

        res = dev_gpo_set(dev, IGPO_BANK_CFG_ADC, 0x0);
        if (res)
            return res;

        for (unsigned adc = 0; adc < 4; adc++) {
            if (fmean[adc] == 0)
                continue;
            fprintf(stderr, "R=%5.1f MHz ADC[%d] CLK=%8d  DIV=%8d\n", crate / 1e6, adc, fmean[adc], fdiv[adc]);
        }
#if 0
        res = check_adc_msk(dev, longtest, gooda, goodb);
        if (res) {
            goto failed;
        }

        for (unsigned adc = 0; adc < 4; adc++) {
            if (fmean[adc] == 0)
                continue;
            fprintf(stderr, "R=%.1f MHz ADC[%d] CLK=%8d  DIV=%8d\n", crate / 1e6, adc, fmean[adc], fdiv[adc]);
        }

        fprintf(stderr, "R=%.1f MHz   PH  ", crate / 1e6);
        for(unsigned p = 0; p < 32; p++) {
            fputc(gooda[p], stderr);
        }
        fputc('|', stderr);
        for(unsigned p = 0; p < 32; p++) {
            fputc(goodb[p], stderr);
        }
        fputc('\n', stderr);
#endif
        res = check_pbrs(dev, longtest, bgooda, bgoodb, bgoodc, bgoodd);
        if (res) {
            goto failed;
        }

        fprintf(stderr, "R=%5.1f MHz (%6d/%6d/%6d/%6d)   PHD ", crate / 1e6,
                fmean[0] - (unsigned)crate, fmean[1] - (unsigned)crate, fmean[2] - (unsigned)crate, fmean[3] - (unsigned)crate);
        for(unsigned a = 0; a < 4; a++) {
            for(unsigned p = 0; p < 32; p++) {
                fputc(bgood_p[a][p] == 0 ? '*' : (bgood_p[a][p] < 1000 ? '-' : ' '), stderr);
            }
            if (a != 3)
                fputc('|', stderr);
            else
                fputc('\n', stderr);
        }

        if (s_exit_event) {
            fprintf(stderr, "INTERRUPTED!\n");
            goto failed;
        }
    } while (iten && ((crate += 2e6) < ratemax));
    } while (cont-- != 0);

    fprintf(stderr, "DONE!\n");
failed:
    tps6594_vout_ctrl(dev, 0, 1, TPS6594_LDO4, false);
    lowlevel_destroy(dev);
    return res;
}
