// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <usdr_logging.h>

#include "ext_fe_ch4_400_7200.h"
#include "../ipblks/gpio.h"
#include "../ipblks/spiext.h"
#include "../hw/tmp114/tmp114.h"
#include "../hw/dac80501/dac80501.h"
#include "../hw/tca6424a/tca6424a.h"

#include "../generic_usdr/generic_regs.h"

#include <def_ext_fe_ch4_400_7200_e.h>
#include <def_ext_fe_ch4_400_7200_usr.h>

// M.2 breakout
// -------------------------------------------------------------------------------------
// M.2 pin     sSDR           dSDR
// 8           SSDR_GPIO2                    M2_1PPS_SYNC   --
//
// 10          SSDR_GPLED0    GPIO33_0       EXT_I2C_SDA
// 20          SSDR_GPIO1     --------       AUX_MUX_GPIO1  -- *EXT2_I2C_SDA / FAN0_TACH
// 38          SSDR_GPLED1_P                 FGPIO_N
// 40          SSDR_GPLED1_N                 FGPIO_P
// 54          SSDR_GPIO6     GPIO33_1       EXT_I2C_SCL
// 56          SSDR_GPIO3_P   --------       n/c GPS_TX
// 57          SSDR_GPIO3_N   --------       n/c GPS_RX
// 68          SSDR_GPIO5     GPIO33_2       AUX_MUX_GPIO0  -- *EXT2_I2C_SCL / FAN1_TACH
//
// I2C3:
//   SSDR_GPLED0  M1
//   SSDR_GPIO6   H2
// I2C4:
//   SSDR_GPIO1   L1
//   SSDR_GPIO5   J2


enum {
    GPIO_1PPS     = GPIO2,

    // GPIO_I2C3_SDA = GPIO12,
    GPIO_I2C3_SCL = GPIO6,

    GPIO_I2C4_SDA = GPIO1,
    GPIO_I2C4_SCL = GPIO5,
};

enum {
    TCA6424A_ADDR_L = 0x22,
    TCA6424A_ADDR_H = 0x23,

    SC18IS606_ADDR = 0b0101111,
};

enum i2c_idx_extra {
    I2C_TCA6424AR_U114 = MAKE_LSOP_I2C_ADDR(1, 0, TCA6424A_ADDR_L),
    I2C_TCA6424AR_U110 = MAKE_LSOP_I2C_ADDR(1, 0, TCA6424A_ADDR_H),
    I2C_TCA6424AR_U300 = MAKE_LSOP_I2C_ADDR(1, 1, TCA6424A_ADDR_L), //AB
    I2C_TCA6424AR_U301 = MAKE_LSOP_I2C_ADDR(1, 1, TCA6424A_ADDR_H), //CD

    I2C_TEMP_U69 = MAKE_LSOP_I2C_ADDR(1, 0, I2C_DEV_TMP114NB),

    I2C_DAC      = MAKE_LSOP_I2C_ADDR(1, 0, 0x48),
};

enum {
    RX_DSA_MAX_ATTN = 15,
};

static const uint64_t s_filerbank_ranges[] = {
    400e6, 1000e6,
    1000e6, 2000e6,
    2000e6, 3500e6,
    2500e6, 5000e6,
    3500e6, 7100e6,
};

//------------------------
// Low level expanders control

static int _ext_fe_ch4_400_7200_exp_upd(ext_fe_ch4_400_7200_t *fe, unsigned addr, unsigned data)
{
    int res = 0;
    switch (addr) {
    case 0x0:
        res = res ? res : tca6424a_reg16_set(fe->dev, fe->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0, data);
        res = res ? res : tca6424a_reg8_set(fe->dev, fe->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0 + 2, data >> 16);
        break;
    case 0x1:
    case 0x2:
    case 0x3:
        res = tca6424a_reg8_set(fe->dev, fe->subdev, I2C_TCA6424AR_U110, TCA6424_OUT0 + (addr - 0x1), data);
        break;
    case 0x4:
    case 0x5:
    case 0x6:
        // TODO: Fast path
        res = tca6424a_reg8_set(fe->dev, fe->subdev, I2C_TCA6424AR_U300, TCA6424_OUT0 + (addr - 0x4), data);
        break;
    case 0x7:
    case 0x8:
    case 0x9:
        // TODO: Fast path
        res = tca6424a_reg8_set(fe->dev, fe->subdev, I2C_TCA6424AR_U301, TCA6424_OUT0 + (addr - 0x7), data);
        break;
    default:
        return -EINVAL;
    }
    return res;
}

static int _ext_fe_ch4_400_7200_exp_get(ext_fe_ch4_400_7200_t *fe, unsigned addr)
{
    int res = 0;
    uint8_t di8 = 0;
    uint16_t di16;
    switch (addr) {
    case 0x0:
        res = res ? res : tca6424a_reg16_get(fe->dev, fe->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0, &di16);
        res = res ? res : tca6424a_reg8_get(fe->dev, fe->subdev, I2C_TCA6424AR_U114, TCA6424_OUT0 + 2, &di8);

        fe->debug_fe_reg_last = (unsigned)di16 | (((unsigned)di8) << 16);

        USDR_LOG("FE4C", USDR_LOG_WARNING, "FE_4CH_EXP RD %02x => %08x\n", addr, fe->debug_fe_reg_last);
        return res;
    case 0x1:
    case 0x2:
    case 0x3:
        res = tca6424a_reg8_get(fe->dev, fe->subdev, I2C_TCA6424AR_U110, TCA6424_OUT0 + (addr - 0x1), &di8);
        break;

    case 0x4:
    case 0x5:
    case 0x6:
        res = tca6424a_reg8_get(fe->dev, fe->subdev, I2C_TCA6424AR_U300, TCA6424_OUT0 + (addr - 0x4), &di8);
        break;

    case 0x7:
    case 0x8:
    case 0x9:
        res = tca6424a_reg8_get(fe->dev, fe->subdev, I2C_TCA6424AR_U301, TCA6424_OUT0 + (addr - 0x7), &di8);
        break;
    default:
        return -EINVAL;
    }

    USDR_LOG("FE4C", USDR_LOG_WARNING, "FE_4CH_EXP RD %02x => %02x\n", addr, di8);
    fe->debug_fe_reg_last = di8;
    return res;
}

//---------------------------------
// high level user friendly control
static void _ext_fe_fbank_map(unsigned filsel, unsigned *bout, unsigned *bin)
{
    // Sanity check YAML <-> internal ABI constants
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_FILT_400_1000M, RX_FB_400_1000M);
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_FILT_1000_2000M, RX_FB_1000_2000M);
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_FILT_2000_3500M, RX_FB_2000_3500M);
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_FILT_2500_5000M, RX_FB_2500_5000M);
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_FILT_3500_7100M, RX_FB_3500_7100M);

    CHECK_CONSTANT_EQ(RX_FILT_OPTS_AUTO_400_1000M, RX_FB_AUTO | RX_FB_400_1000M);
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_AUTO_1000_2000M, RX_FB_AUTO | RX_FB_1000_2000M);
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_AUTO_2000_3500M, RX_FB_AUTO | RX_FB_2000_3500M);
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_AUTO_2500_5000M, RX_FB_AUTO | RX_FB_2500_5000M);
    CHECK_CONSTANT_EQ(RX_FILT_OPTS_AUTO_3500_7100M, RX_FB_AUTO | RX_FB_3500_7100M);

    CHECK_CONSTANT_EQ(SW_RX_FILTER_IN_CHA_400_1000M, SW_RX_FILTER_OUT_CHA_400_1000M);

    unsigned fb_f_sel = (~RX_FB_AUTO & filsel);
    switch (fb_f_sel) {
    case RX_FB_400_1000M: *bout = SW_RX_FILTER_OUT_CHA_400_1000M; *bin = SW_RX_FILTER_IN_CHA_400_1000M; break;
    case RX_FB_1000_2000M: *bout = SW_RX_FILTER_OUT_CHA_1000_2000M; *bin = SW_RX_FILTER_IN_CHA_1000_2000M; break;
    case RX_FB_2000_3500M: *bout = SW_RX_FILTER_OUT_CHA_2000_3500M; *bin = SW_RX_FILTER_IN_CHA_2000_3500M; break;
    case RX_FB_2500_5000M: *bout = SW_RX_FILTER_OUT_CHA_2500_5000M; *bin = SW_RX_FILTER_IN_CHA_2500_5000M; break;
    case RX_FB_3500_7100M: *bout = SW_RX_FILTER_OUT_CHA_3500_7100M; *bin = SW_RX_FILTER_IN_CHA_3500_7100M; break;
    default: *bout = SW_RX_FILTER_OUT_CHA_MUTE1; *bin = SW_RX_FILTER_IN_CHA_MUTE1; break;
    }
}

// Switch on RX path =>  ANT_RX external port / rfsw_rxtx / LB
enum rfsw_tddfdd_bits {
    EXP_TDDFDD_SD           = 0b00, // LB SW is on
    EXP_TDDFDD_P1_LB_SW     = 0b01, // LB SW is on
    EXP_TDDFDD_P2_TRX_SW    = 0b10,
    EXP_TDDFDD_P3_ANT_RX    = 0b11,
};

// Switch on ANT_TRX external port =>  rfsw_tddfdd / rfsw_tx_onoff
enum rfsw_rxtx_bits {
    EXP_RXTX_SW_P2_RX = 1,
    EXP_RXTX_SW_P1_TX = 0,
};

// Switch TX path => rfsw_rxtx / LB
enum rfsw_tx_onoff_bits {
    EXP_TX_ONOFF_P2_LB_SW  = 1,
    EXP_TX_ONOFF_P1_TRX_SW = 0,
};

enum led_rtx_vals {
    LED_TRX_RXO = 0,
    LED_TRX_OFF = 1,
    LED_TRX_TRX = 2,
    LED_TRX_TXO = 3,
};

enum led_rx_cals {
    LED_RX_ON = 0,
    LED_RX_OFF = 1,
};

static void _ext_fe_antenna_sw_map_exp(unsigned antenna, bool rxen, bool txen,
                                       uint8_t* exp_tddfdd, uint8_t* exp_rxtx, uint8_t* exp_tx_onoff,
                                       uint8_t* exp_led_trx, uint8_t* exp_led_rx,
                                       unsigned *arx, unsigned *atx)
{
    CHECK_CONSTANT_EQ(ANT_OPTS_RX_TO_RX_AND_TX_TO_TRX, ANT_RX_TRX);
    CHECK_CONSTANT_EQ(ANT_OPTS_RX_TO_TRX_AND_TX_TERM, ANT_TRX_TERM);
    CHECK_CONSTANT_EQ(ANT_OPTS_RX_TO_RX_AND_TX_TERM, ANT_RX_TERM);
    CHECK_CONSTANT_EQ(ANT_OPTS_RX_TX_LOOPBACK, ANT_LOOPBACK);
    CHECK_CONSTANT_EQ(ANT_OPTS_TDD_DRIVEN_AUTO, ANT_HW_TDD);

    switch (antenna) {
    case ANT_RX_TRX:
        *exp_tx_onoff = EXP_TX_ONOFF_P1_TRX_SW;
        *exp_rxtx = EXP_RXTX_SW_P1_TX;
        *exp_tddfdd = EXP_TDDFDD_P3_ANT_RX;
        *exp_led_trx = LED_TRX_TXO;
        *exp_led_rx = LED_RX_ON;
        *arx = rxen;
        *atx = txen;
        break;

    case ANT_TRX_TERM:
        *exp_tx_onoff = EXP_TX_ONOFF_P2_LB_SW;
        *exp_rxtx = EXP_RXTX_SW_P2_RX;
        *exp_tddfdd = EXP_TDDFDD_P2_TRX_SW;
        *exp_led_trx = LED_TRX_RXO;
        *exp_led_rx = LED_RX_OFF;
        *arx = rxen;
        *atx = 0;
        break;

    case ANT_RX_TERM:
        *exp_tx_onoff = EXP_TX_ONOFF_P2_LB_SW;
        *exp_rxtx = EXP_RXTX_SW_P1_TX;
        *exp_tddfdd = EXP_TDDFDD_P3_ANT_RX;
        *exp_led_trx = LED_TRX_OFF;
        *exp_led_rx = LED_RX_ON;
        *arx = rxen;
        *atx = 0;
        break;

    case ANT_LOOPBACK:
        *exp_tx_onoff = EXP_TX_ONOFF_P2_LB_SW;
        *exp_rxtx = EXP_RXTX_SW_P1_TX;
        *exp_tddfdd = EXP_TDDFDD_P1_LB_SW;
        *exp_led_trx = LED_TRX_OFF;
        *exp_led_rx = LED_RX_OFF;
        *arx = rxen;
        *atx = txen;
        break;

    case ANT_HW_TDD:
        // TODO
        *exp_led_trx = LED_TRX_TRX;
        *exp_led_rx = LED_RX_OFF;
        *arx = rxen;
        *atx = txen;
        break;

    default:
        *exp_tx_onoff = EXP_TX_ONOFF_P2_LB_SW;
        *exp_rxtx = EXP_RXTX_SW_P1_TX;
        *exp_tddfdd = EXP_TDDFDD_P1_LB_SW;

        *exp_led_trx = LED_TRX_OFF;
        *exp_led_rx = LED_RX_OFF;
        *arx = 0;
        *atx = 0;
        break;
    }
}

void ext_fe_rx_filterbank_upd(ext_fe_ch4_400_7200_t* def, unsigned chno)
{
    if (def->ucfg[chno].rx_fb_sel < RX_FILT_OPTS_AUTO_400_1000M)
        return;

    unsigned best_idx = 0;
    unsigned best_off = 1000;

    for (unsigned i = 0; i < SIZEOF_ARRAY(s_filerbank_ranges); i+= 2) {
        if (s_filerbank_ranges[i] > def->ucfg[chno].rx_freq || def->ucfg[chno].rx_freq > s_filerbank_ranges[i + 1]) {
            continue;
        }

        int64_t doff = (int64_t)(s_filerbank_ranges[i] + s_filerbank_ranges[i + 1]) / 2 - def->ucfg[chno].rx_freq ;
        if (doff < 0)
            doff = 0 - doff;

        unsigned off = 1000 * doff / (s_filerbank_ranges[i + 1] - s_filerbank_ranges[i]);
        if (off < best_off) {
            best_off = off;
            best_idx = i / 2;
        }

        USDR_LOG("FE4C", USDR_LOG_WARNING, "F%d %.3f -- %.3f  DOFF=%u OFF=%u\n",
                 i, s_filerbank_ranges[i] / 1.0e6, s_filerbank_ranges[i + 1] / 1.0e6, (unsigned)doff, off);
    }

    def->ucfg[chno].rx_fb_sel = RX_FILT_OPTS_AUTO_400_1000M | best_idx;
    USDR_LOG("FE4C", USDR_LOG_WARNING, "RXFBabk[%d] = %d\n", chno, def->ucfg[chno].rx_fb_sel);
}

// This function just update states of internal HW and I2C expander registers,
// all calculation of band, filter, lofreq, etc. has been done before this call
int ext_fe_update_user(ext_fe_ch4_400_7200_t* fe)
{
    unsigned fbanksel_out[FE_MAX_HW_CHANS];
    unsigned fbanksel_in[FE_MAX_HW_CHANS];

    uint8_t exp_tx_onoff[FE_MAX_HW_CHANS];
    uint8_t exp_rxtx[FE_MAX_HW_CHANS];
    uint8_t exp_tddfdd[FE_MAX_HW_CHANS];

    unsigned act_rx[FE_MAX_HW_CHANS];
    unsigned act_tx[FE_MAX_HW_CHANS];

    uint8_t rx_led[FE_MAX_HW_CHANS];
    uint8_t trx_led[FE_MAX_HW_CHANS];

    uint8_t enanble_rx = 0;
    uint8_t enanble_tx = 0;

    int res = 0;

    for (unsigned i = 0; i < FE_MAX_HW_CHANS; i++) {
        unsigned rxen = fe->ucfg[i].rx_en;
        unsigned txen = fe->ucfg[i].tx_en;

        // RX filterbank
        _ext_fe_fbank_map(fe->ucfg[i].rx_fb_sel, &fbanksel_out[i], &fbanksel_in[i]); // SW_RX_FILTER_OUT_CHA_MUTE1 if not enabled?

        // Antanna switch, RF PA/LNA switch, loopback switch
        _ext_fe_antenna_sw_map_exp(fe->ucfg[i].ant_sel, rxen, txen, &exp_tddfdd[i], &exp_rxtx[i], &exp_tx_onoff[i],
                                   &trx_led[i], &rx_led[i], &act_rx[i], &act_tx[i]);

        enanble_rx |= rxen;
        enanble_tx |= txen;
    };

    // RX filter Bank
    fe->fe_exp_regs[0] = MAKE_EXT_FE_CH4_400_7200_E_SW_RX_FILTER(
        fbanksel_in[H_CHD], fbanksel_in[H_CHC], fbanksel_in[H_CHB], fbanksel_in[H_CHA],
        fbanksel_out[H_CHA], fbanksel_out[H_CHB], fbanksel_out[H_CHC], fbanksel_out[H_CHD]);

    // Gobal enable
    fe->fe_exp_regs[1] = MAKE_EXT_FE_CH4_400_7200_E_ENABLE(
        fe->ucfg[H_CHD].tx_ss, fe->ucfg[H_CHC].tx_ss, fe->ucfg[H_CHB].tx_ss, fe->ucfg[H_CHA].tx_ss,
        fe->if_vbyp, fe->ref_gps, enanble_tx, enanble_rx);

    // TRX leds
    fe->fe_exp_regs[2] = MAKE_EXT_FE_CH4_400_7200_E_LED_TRX_CTRL(trx_led[H_CHD], trx_led[H_CHC], trx_led[H_CHB], trx_led[H_CHA]);
    fe->fe_exp_regs[3] = MAKE_EXT_FE_CH4_400_7200_E_LEDRX_CH_CTRL(
        rx_led[H_CHD], rx_led[H_CHC], rx_led[H_CHB], rx_led[H_CHA],
        fe->ucfg[H_CHD].rx_en, fe->ucfg[H_CHC].rx_en, fe->ucfg[H_CHB].rx_en, fe->ucfg[H_CHA].rx_en);


    // Expander & HIGH-speed IO
    // AB / CD control pairs
    for (unsigned pair = 0; pair < 2; pair++) {
        unsigned idx = P_A_EN_AB - SW_RX_FILTER + (P_A_EN_CD - P_A_EN_AB) * pair;

        fe->fe_exp_regs[idx + 0] = MAKE_EXT_FE_CH4_400_7200_E_REG_WR(idx + SW_RX_FILTER, MAKE_EXT_FE_CH4_400_7200_E_P_A_EN_AB(
                                                                                             act_tx[2 * pair + 0], act_tx[2 * pair + 1]));
        fe->fe_exp_regs[idx + 1] = MAKE_EXT_FE_CH4_400_7200_E_REG_WR(idx + SW_RX_FILTER, MAKE_EXT_FE_CH4_400_7200_E_ATTN_RX_CH_AB(
                                                                                             fe->ucfg[2 * pair + 0].rx_dsa, fe->ucfg[2 * pair + 1].rx_dsa));

        fe->fe_exp_regs[idx + 2] = MAKE_EXT_FE_CH4_400_7200_E_REG_WR(idx + SW_RX_FILTER, MAKE_EXT_FE_CH4_400_7200_E_SW_AB(
                                                                                             exp_rxtx[2 * pair + 1], exp_tx_onoff[2 * pair + 1],
                                                                                             exp_rxtx[2 * pair + 0], exp_tx_onoff[2 * pair + 0],
                                                                                             exp_tddfdd[2 * pair + 0], exp_tddfdd[2 * pair + 1]));
    }

    // TODO add control for high speed IO
    for (unsigned addr = 0; addr < FE_CTRL_REGS; addr++) {
        res = res ? res : _ext_fe_ch4_400_7200_exp_upd(fe, addr, fe->fe_exp_regs[addr]);
    }
    return res;
}


int ext_fe_rx_freq_set(ext_fe_ch4_400_7200_t* def, unsigned chno, uint64_t freq)
{
    if (chno >= FE_MAX_HW_CHANS)
        return -EINVAL;
    if (!def->ucfg[chno].rx_en)
        return 0;

    def->ucfg[chno].rx_freq = freq;

    ext_fe_rx_filterbank_upd(def, chno);
    return ext_fe_update_user(def);
}

int ext_fe_rx_chan_en(ext_fe_ch4_400_7200_t* def, unsigned ch_fe_mask_rx)
{
    for (unsigned i = 0; i < FE_MAX_HW_CHANS; i++) {
        def->ucfg[i].rx_en = (ch_fe_mask_rx & (1u << i)) ? 1 : 0;
    }
    return ext_fe_update_user(def);
}

int ext_fe_tx_chan_en(ext_fe_ch4_400_7200_t* def, unsigned ch_fe_mask_tx)
{
    for (unsigned i = 0; i < FE_MAX_HW_CHANS; i++) {
        def->ucfg[i].tx_en = (ch_fe_mask_tx & (1u << i)) ? 1 : 0;
    }
    return ext_fe_update_user(def);
}

int ext_fe_rx_gain_set(ext_fe_ch4_400_7200_t* def, unsigned chno, unsigned gain, unsigned* actual_gain)
{
    if (chno >= FE_MAX_HW_CHANS)
        return -EINVAL;
    if (!def->ucfg[chno].rx_en)
        return 0;
    if (gain > RX_DSA_MAX_ATTN)
        gain = RX_DSA_MAX_ATTN;

    def->ucfg[chno].rx_dsa = RX_DSA_MAX_ATTN - gain;

    if (actual_gain) {
        *actual_gain = gain;
    }

    return ext_fe_update_user(def);
}



int ext_fe_ch4_sens_get(ext_fe_ch4_400_7200_t* fe, uint64_t *ovalue)
{
    int temp256 = 127*256, res;
    res = tmp114_temp_get(fe->dev, fe->subdev, I2C_TEMP_U69, &temp256);
    *ovalue = (int64_t)temp256;
    return res;
}




static int ext_fe_ch4_400_7200_ctrl_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    ext_fe_ch4_400_7200_t* fe = (ext_fe_ch4_400_7200_t*)obj->object;
    int res;
    unsigned addr = (value >> 24) & 0x7f;
    unsigned data = value & 0xffffff;

    fe->debug_fe_reg_last = ~0u;

    if (value & 0x80000000) {
        USDR_LOG("FE4C", USDR_LOG_WARNING, "FE_CH4_CTRL %08x => %08x\n", addr, data);

        if (addr < SW_RX_FILTER || addr >= SW_RX_FILTER + FE_CTRL_REGS) {
            return -EINVAL;
        }
        res = _ext_fe_ch4_400_7200_exp_upd(fe, addr - SW_RX_FILTER, data);
    } else {
        res = _ext_fe_ch4_400_7200_exp_get(fe, addr - SW_RX_FILTER);
    }

    return res;
}

static int ext_fe_ch4_400_7200_ctrl_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    ext_fe_ch4_400_7200_t* fe = (ext_fe_ch4_400_7200_t*)obj->object;
    *ovalue = fe->debug_fe_reg_last;
    return 0;
}

int ext_fe_ch4_400_7200_temp_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    return ext_fe_ch4_sens_get((ext_fe_ch4_400_7200_t*)obj->object, ovalue);
}


static int ext_fe_ch4_400_7200_usr_reg_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    ext_fe_ch4_400_7200_t* fe = (ext_fe_ch4_400_7200_t*)obj->object;
    int res = 0;
    unsigned addr = (value >> 24) & 0x7f;
    unsigned data = value & 0xffffff;

    fe->debug_fe_usr_last = ~0u;

    if (value & 0x80000000) {
        USDR_LOG("FE4C", USDR_LOG_WARNING, "EXFE_4CH_USER %08x => %08x\n", addr, data);

        switch (addr) {
        case RX_FILTER_BANK:
            fe->ucfg[H_CHA].rx_fb_sel = GET_EXT_FE_CH4_400_7200_USR_RX_FILTER_BANK_A(data);
            fe->ucfg[H_CHB].rx_fb_sel = GET_EXT_FE_CH4_400_7200_USR_RX_FILTER_BANK_B(data);
            fe->ucfg[H_CHC].rx_fb_sel = GET_EXT_FE_CH4_400_7200_USR_RX_FILTER_BANK_C(data);
            fe->ucfg[H_CHD].rx_fb_sel = GET_EXT_FE_CH4_400_7200_USR_RX_FILTER_BANK_D(data);
            break;
        case RX_ATTN:
            fe->ucfg[H_CHA].rx_dsa = GET_EXT_FE_CH4_400_7200_USR_RX_ATTN_A(data);
            fe->ucfg[H_CHB].rx_dsa = GET_EXT_FE_CH4_400_7200_USR_RX_ATTN_B(data);
            fe->ucfg[H_CHC].rx_dsa = GET_EXT_FE_CH4_400_7200_USR_RX_ATTN_C(data);
            fe->ucfg[H_CHD].rx_dsa = GET_EXT_FE_CH4_400_7200_USR_RX_ATTN_D(data);
            break;
        case ANT_SEL:
            fe->ucfg[H_CHA].ant_sel = GET_EXT_FE_CH4_400_7200_USR_ANT_SEL_A(data);
            fe->ucfg[H_CHB].ant_sel = GET_EXT_FE_CH4_400_7200_USR_ANT_SEL_B(data);
            fe->ucfg[H_CHC].ant_sel = GET_EXT_FE_CH4_400_7200_USR_ANT_SEL_C(data);
            fe->ucfg[H_CHD].ant_sel = GET_EXT_FE_CH4_400_7200_USR_ANT_SEL_D(data);
            break;
        case RX_CHEN:
            fe->ucfg[H_CHA].rx_en = GET_EXT_FE_CH4_400_7200_USR_RX_CHEN_A(data);
            fe->ucfg[H_CHB].rx_en = GET_EXT_FE_CH4_400_7200_USR_RX_CHEN_B(data);
            fe->ucfg[H_CHC].rx_en = GET_EXT_FE_CH4_400_7200_USR_RX_CHEN_C(data);
            fe->ucfg[H_CHD].rx_en = GET_EXT_FE_CH4_400_7200_USR_RX_CHEN_D(data);
            break;
        case TX_CHEN:
            fe->ucfg[H_CHA].tx_en = GET_EXT_FE_CH4_400_7200_USR_TX_CHEN_A(data);
            fe->ucfg[H_CHB].tx_en = GET_EXT_FE_CH4_400_7200_USR_TX_CHEN_B(data);
            fe->ucfg[H_CHC].tx_en = GET_EXT_FE_CH4_400_7200_USR_TX_CHEN_C(data);
            fe->ucfg[H_CHD].tx_en = GET_EXT_FE_CH4_400_7200_USR_TX_CHEN_D(data);
            break;
        case TX_2STAGE:
            fe->ucfg[H_CHA].tx_ss = GET_EXT_FE_CH4_400_7200_USR_TX_2STAGE_A(data);
            fe->ucfg[H_CHB].tx_ss = GET_EXT_FE_CH4_400_7200_USR_TX_2STAGE_B(data);
            fe->ucfg[H_CHC].tx_ss = GET_EXT_FE_CH4_400_7200_USR_TX_2STAGE_C(data);
            fe->ucfg[H_CHD].tx_ss = GET_EXT_FE_CH4_400_7200_USR_TX_2STAGE_D(data);
            break;
        default:
            return -EINVAL;
        }

        // Update state
        res = ext_fe_update_user(fe);
    } else {
        switch (addr) {
        case RX_FILTER_BANK:
            fe->debug_fe_usr_last = MAKE_EXT_FE_CH4_400_7200_USR_RX_FILTER_BANK(
                fe->ucfg[H_CHD].rx_fb_sel, fe->ucfg[H_CHC].rx_fb_sel, fe->ucfg[H_CHB].rx_fb_sel, fe->ucfg[H_CHA].rx_fb_sel);
            break;
        case RX_ATTN:
            fe->debug_fe_usr_last = MAKE_EXT_FE_CH4_400_7200_USR_RX_ATTN(
                fe->ucfg[H_CHD].rx_dsa, fe->ucfg[H_CHC].rx_dsa, fe->ucfg[H_CHB].rx_dsa, fe->ucfg[H_CHA].rx_dsa);
            break;
        case ANT_SEL:
            fe->debug_fe_usr_last = MAKE_EXT_FE_CH4_400_7200_USR_ANT_SEL(
                fe->ucfg[H_CHD].ant_sel, fe->ucfg[H_CHC].ant_sel, fe->ucfg[H_CHB].ant_sel, fe->ucfg[H_CHA].ant_sel);
            break;
        case RX_CHEN:
            fe->debug_fe_usr_last = MAKE_EXT_FE_CH4_400_7200_USR_RX_CHEN(
                fe->ucfg[H_CHD].rx_en, fe->ucfg[H_CHC].rx_en, fe->ucfg[H_CHB].rx_en, fe->ucfg[H_CHA].rx_en);
            break;
        case TX_CHEN:
            fe->debug_fe_usr_last = MAKE_EXT_FE_CH4_400_7200_USR_TX_CHEN(
               fe->ucfg[H_CHD].tx_en, fe->ucfg[H_CHC].tx_en, fe->ucfg[H_CHB].tx_en, fe->ucfg[H_CHA].tx_en);
            break;
        case TX_2STAGE:
            fe->debug_fe_usr_last = MAKE_EXT_FE_CH4_400_7200_USR_TX_2STAGE(
                fe->ucfg[H_CHD].tx_ss, fe->ucfg[H_CHC].tx_ss, fe->ucfg[H_CHB].tx_ss, fe->ucfg[H_CHA].tx_ss);
            break;
        default:
            return -EINVAL;
        };
    }

    return res;
}

static int ext_fe_ch4_400_7200_usr_reg_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    ext_fe_ch4_400_7200_t* fe = (ext_fe_ch4_400_7200_t*)obj->object;
    *ovalue = fe->debug_fe_usr_last;

    return 0;
}


static const usdr_dev_param_func_t s_fe_parameters[] = {
    { "/dm/sensor/temp_fe0",             { NULL, ext_fe_ch4_400_7200_temp_get }},
    { "/debug/hw/exfe10_4ch_exp/0/reg" , { ext_fe_ch4_400_7200_ctrl_reg_set, ext_fe_ch4_400_7200_ctrl_reg_get } },
    { "/debug/hw/exfe10_4ch_usr/0/reg" , { ext_fe_ch4_400_7200_usr_reg_set, ext_fe_ch4_400_7200_usr_reg_get } },
};



int ext_fe_ch4_400_7200_init(lldev_t dev,
                             unsigned subdev,
                             unsigned gpio_base,
                             const char *params,
                             const char *compat,
                             ext_fe_ch4_400_7200_t* ob)
{
    int res = 0;
    int val = 0;
    uint16_t val16[4] = { 0xbad, 0xbad, 0xbad, 0xbad };
    device_t* base = lowlevel_get_device(dev);
    ob->dev = dev;
    ob->subdev = subdev;

    USDR_LOG("FE4C", USDR_LOG_INFO, "Initializing FE_4CH_400_7200 front end...\n");

    // if (strcmp(compat, "m2m") != 0) {
    //     return -ENODEV;
    // }
    // TODO sSDR/dSDR specific

    // Configure external I2C bus
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_I2C3_SCL, GPIO_CFG_ALT1);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_I2C4_SDA, GPIO_CFG_ALT1);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_I2C4_SCL, GPIO_CFG_ALT1);
    if (res)
        return res;

    res = (res) ? res : tmp114_devid_get(dev, subdev, I2C_TEMP_U69, &val);

    res = (res) ? res : tca6424a_reg16_get(dev, subdev, I2C_TCA6424AR_U114, TCA6424_CFG0, &val16[0]);
    res = (res) ? res : tca6424a_reg16_get(dev, subdev, I2C_TCA6424AR_U110, TCA6424_CFG0, &val16[1]);
    res = (res) ? res : tca6424a_reg16_get(dev, subdev, I2C_TCA6424AR_U300, TCA6424_CFG0, &val16[2]);
    res = (res) ? res : tca6424a_reg16_get(dev, subdev, I2C_TCA6424AR_U301, TCA6424_CFG0, &val16[3]);

    USDR_LOG("FE4C", USDR_LOG_ERROR, "Temp ID = %4x, {U114/U110/U300/U301}_Cfg0 = %4x/%4x/%4x/%4x\n", val,
             val16[0], val16[1], val16[2], val16[3]);

    if (res || val != TMP114_DEVICE_ID)
        return res;

    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_1PPS, GPIO_CFG_ALT0);

    res = (res) ? res : tca6424a_reg16_set(dev, subdev, I2C_TCA6424AR_U114, TCA6424_OUT0, 0);
    res = (res) ? res : tca6424a_reg8_set(dev, subdev, I2C_TCA6424AR_U114, TCA6424_OUT0 + 2, 0);
    res = (res) ? res : tca6424a_reg16_set(dev, subdev, I2C_TCA6424AR_U110, TCA6424_OUT0, 0);
    res = (res) ? res : tca6424a_reg8_set(dev, subdev, I2C_TCA6424AR_U110, TCA6424_OUT0 + 2, 0);

    res = (res) ? res : tca6424a_reg16_set(dev, subdev, I2C_TCA6424AR_U114, TCA6424_CFG0, 0);
    res = (res) ? res : tca6424a_reg8_set(dev, subdev, I2C_TCA6424AR_U114, TCA6424_CFG0 + 2, 0);
    res = (res) ? res : tca6424a_reg16_set(dev, subdev, I2C_TCA6424AR_U110, TCA6424_CFG0, 0);
    res = (res) ? res : tca6424a_reg8_set(dev, subdev, I2C_TCA6424AR_U110, TCA6424_CFG0 + 2, 0);

    res = (res) ? res : tmp114_temp_get(dev, subdev, I2C_TEMP_U69, &val);
    USDR_LOG("FE4C", USDR_LOG_ERROR, "Temp %.2fC\n", val / 256.0);

    // User initialization
    ob->ref_gps = 1;
    ob->if_vbyp = 1;
    for (unsigned ch = 0; ch < FE_MAX_HW_CHANS; ch++) {
        ob->ucfg[ch].rx_fb_sel = RX_FB_AUTO; // rx_filterbank
        ob->ucfg[ch].rx_dsa = 0;
        ob->ucfg[ch].ant_sel = ANT_OFF;
        ob->ucfg[ch].tx_ss = 0; // Single stage PA
        ob->ucfg[ch].tx_en = 0; // Channel enabled on device side
        ob->ucfg[ch].rx_en = 0; // Channel enabled on device side
        ob->ucfg[ch].rx_freq = 0;
    }

    res = (res) ? res : ext_fe_update_user(ob);

    // TODO: HighSpeed IO
    res = (res) ? res : tca6424a_reg16_set(dev, subdev, I2C_TCA6424AR_U300, TCA6424_CFG0, 0);
    res = (res) ? res : tca6424a_reg8_set(dev, subdev, I2C_TCA6424AR_U300, TCA6424_CFG0 + 2, 0);
    res = (res) ? res : tca6424a_reg16_set(dev, subdev, I2C_TCA6424AR_U301, TCA6424_CFG0, 0);
    res = (res) ? res : tca6424a_reg8_set(dev, subdev, I2C_TCA6424AR_U301, TCA6424_CFG0 + 2, 0);


    res = (res) ? res : dac80501_init(dev, subdev, I2C_DAC, DAC80501_CFG_REF_DIV_GAIN_MUL);
    if (res) {
        USDR_LOG("FE4C", USDR_LOG_WARNING, "External DAC not recognized error=%d\n", res);
        //return -ENODEV;
        // ob->dac_present = false;
    }

    res = (res) ? res : usdr_vfs_obj_param_init_array_param(base,
                                              (void*)ob,
                                              s_fe_parameters,
                                              SIZEOF_ARRAY(s_fe_parameters));
    if (res)
        return res;

    ob->dev = dev;
    ob->subdev = subdev;
    ob->gpio_base = gpio_base;
    ob->hsgpio_base = 0;
    return 0;
}

int ext_fe_destroy(ext_fe_ch4_400_7200_t* dfe)
{
    for (unsigned ch = 0; ch < FE_MAX_HW_CHANS; ch++) {
        dfe->ucfg[ch].tx_en = 0; // Channel enabled on device side
        dfe->ucfg[ch].rx_en = 0; // Channel enabled on device side
    }

    return ext_fe_update_user(dfe);
}

int ext_fe_set_dac(ext_fe_ch4_400_7200_t* brd, unsigned value)
{
    USDR_LOG("M2PE", USDR_LOG_ERROR, "DAC set to: %d\n", value);
    return dac80501_dac_set(brd->dev, brd->subdev, I2C_DAC, value);
}
