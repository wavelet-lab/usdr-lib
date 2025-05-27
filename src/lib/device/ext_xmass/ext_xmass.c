// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "ext_xmass.h"
#include "../ipblks/gpio.h"

#include <stdlib.h>
#include <usdr_logging.h>
#include <string.h>

#include "../hw/tca9555/tca9555.h"
#include "../hw/lmk05318/lmk05318.h"

#include "def_ext_xmass_ctrl.h"


enum {
    XMASS_GPIO_RF_CAL_DST_SEL = 2, // 0 - RF_CAL_EXT (general rx port) / 1 - RF_CAL_INT (LNA3 port)
    XMASS_GPIO_RF_CAL_SRC_SEL = 3, // 0 - RF_LO_SRC (from LMK)         / 1 - RF_NOISE_SRC (from NOISE GEN)

    XMASS_GPIO_RF_CAL_SW = 9,      // 0 - Use RF cal source as FB, 1 - Use XSDR TX as FB
    XMASS_GPIO_RF_LB_SW = 10,      // 0 - Normal operation, 1 - use LB path to XSDR RX
    XMASS_GPIO_RF_NOISE_EN = 11,   // Enable 14V generator for Zener
};

// XRA1201IL24TR-F (0x28 I2C)
// Port0:
//  0 GPIO_BDISTRIB    : Enable OUT_REF_B[0:3] and OUT_SYREF_B[0:3] for 4 board sync
//  1 GPIO_BLOCAL      : Enable local PPS and REF distribution
//  2 RF_CAL_DST_SEL   : 0 - RF_CAL_EXT (general rx port) / 1 - RF_CAL_INT (LNA3 port)
//  3 RF_CAL_SRC_SEL   : 0 - RF_LO_SRC (from LMK)         / 1 - RF_NOISE_SRC (from NOISE GEN)
//  4 GPS_PWREN        : Enable GPS module + DC-bias
//  5 GPIO_SYNC        : LMK05318B GPIO0/SYNC_N
//  6 SYSREF_1PPS_SEL  : 0 - LMK_1PPS, 1 - From SDR_A
//  7 EN_LMX           : Enable LMK05318B
//
// Port1:
//  8 RF_EN            : Enables Power Amplifiers
//  9 RF_CAL_SW        : 0 - Use RF cal source as FB, 1 - Use XSDR TX as FB
// 10 RF_LB_SW         : 0 - Normal operation, 1 - use LB path to XSDR RX
// 11 RF_NOISE_EN      : Enable 12V generator for Zener
// 12 SYSREF_GPSRX_SEL : TX_SYREF_MUX => demuliplexed to ( == 0) ? CLK_SYSREF_OUT : GPS_RX
// 13 M3_RTS
// 14 M2_RTS
// 15 M1_RTS
//
// LMK05318 (0x65 I2C)
//    OUT[0:3] unused
//    OUT4     RF   / LVDS
//    OUT5     aux_p + aux_n
//    OUT6     REF  / LVCMOS
//    OUT7     1PPS / LVCMOS
//
// M2_Connector (master)
// LED1/SDA   CLK_SDA
// LED1/SCL   CLK_SCL
// GPIO0/SDA  M1_CTS
// GPIO1/SCL  M2_CTS
// GPIO3/RX   TX_SYREF_MUX
// GPIO2/PPS  1PPS_IN
// GPIO4/TX   GPS_TX
// GPIO5      M3_RXD
// GPIO6      M3_TXD
// GPIO7      M3_CTS
// GPIO8      M1_TXD
// GPIO9      M1_RXD
// GPIO10     M2_TXD
// GPIO11     M2_RXD
//
// M2_Connector (slaves)
// GPIO2/PPS  1PPS_IN
// GPIO8      Mx_RXD
// GPIO9      Mx_RTS
// GPIO10     Mx_CTS
// GPIO11     Mx_TXD


enum {
    I2C_ADDR_LMK = 0x65,
    I2C_ADDR_XRA1201 = 0x14,
    I2C_GPS_RX = 0x20,
    I2C_GPS_TX = 0x21,
};

static int _board_xmass_fill_lmk05318(board_xmass_t* ob, lmk05318_out_config_t lmk05318_outs_cfg[8])
{
    unsigned cfreq = (ob->calfreq < 3.1e6) ? 0 : ob->calfreq;
    lmk05318_port_request(&lmk05318_outs_cfg[0], 0, 0, false, OUT_OFF);
    lmk05318_port_request(&lmk05318_outs_cfg[1], 1, 0, false, OUT_OFF);
    lmk05318_port_request(&lmk05318_outs_cfg[2], 2, 0, false, OUT_OFF);
    lmk05318_port_request(&lmk05318_outs_cfg[3], 3, 0, false, OUT_OFF);
    lmk05318_port_request(&lmk05318_outs_cfg[4], 4, cfreq, false, cfreq == 0 ? OUT_OFF : LVDS);
    lmk05318_port_request(&lmk05318_outs_cfg[5], 5, 0, false, OUT_OFF);
    lmk05318_port_request(&lmk05318_outs_cfg[6], 6, ob->refclk, false, LVCMOS_P_N);
    // lmk05318_port_request(&lmk05318_outs_cfg[7], 7, 1, false, LVCMOS_P_N);
    lmk05318_port_request(&lmk05318_outs_cfg[7], 7, 0, false, OUT_OFF);

    lmk05318_set_port_affinity(&lmk05318_outs_cfg[4], AFF_APLL2);
    return 0;
}

int board_xmass_init(lldev_t dev,
                     unsigned subdev,
                     unsigned gpio_base,
                     const char* compat,
                     unsigned int i2c_loc,
                     board_xmass_t* ob)
{
    int res = 0;

    // This breakout is compatible with M.2 key A/E or A+E boards
    if ((strcmp(compat, "m2a+e") != 0) && (strcmp(compat, "m2e") != 0) && (strcmp(compat, "m2a") != 0))
        return -ENODEV;


    // Configure external SDA/SCL
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO0, GPIO_CFG_IN);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO1, GPIO_CFG_IN);

    // Configure 1PPS input
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO2, GPIO_CFG_ALT0);

    unsigned i2c_lmka = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(i2c_loc), LSOP_I2C_BUSNO(i2c_loc), I2C_ADDR_LMK);
    unsigned i2c_xraa = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(i2c_loc), LSOP_I2C_BUSNO(i2c_loc), I2C_ADDR_XRA1201);
    uint16_t val;
    uint16_t out_msk = 0xe000;

    res = (res) ? res : tca9555_reg16_get(dev, subdev, i2c_xraa, TCA9555_CFG0, &val);
    res = (res) ? res : tca9555_reg16_set(dev, subdev, i2c_xraa, TCA9555_CFG0, out_msk);
    res = (res) ? res : tca9555_reg16_get(dev, subdev, i2c_xraa, TCA9555_CFG0, &val);

    if (res)
        return res;

    if (val != out_msk) {
        USDR_LOG("XMSS", USDR_LOG_INFO, "GPIO expander initialization failed! Reported mask %04x != %04x\n", val, out_msk);
        return -ENODEV;
    }

    // En LMK to ckeck it
    res = (res) ? res : tca9555_reg16_set(dev, subdev, i2c_xraa, TCA9555_OUT0, (3) | (1 << 4) | (1 << 8) | (1 << 7) | (1 << 5));

    usleep(250000);

    ob->refclk = 25e6;
    ob->calfreq = 444e6;

    //LMK05318 init start
    lmk05318_xo_settings_t xo;
    memset(&xo, 0, sizeof(xo));
    xo.fref = 26000000;
    xo.type = XO_AC_DIFF_EXT;

    lmk05318_dpll_settings_t dpll;
    memset(&dpll, 0, sizeof(dpll));
    dpll.enabled = false;
    dpll.en[LMK05318_PRIREF] = true;
    dpll.fref[LMK05318_PRIREF] = 1;
    dpll.type[LMK05318_PRIREF] = DPLL_REF_TYPE_DIFF_NOTERM;
    dpll.dc_mode[LMK05318_PRIREF] = DPLL_REF_DC_COUPLED_INT;
    dpll.buf_mode[LMK05318_PRIREF] = DPLL_REF_AC_BUF_HYST50_DC_EN;

    lmk05318_out_config_t lmk05318_outs_cfg[8];
    res = res ? res : _board_xmass_fill_lmk05318(ob, lmk05318_outs_cfg);
    res = res ? res : lmk05318_create(dev, subdev, i2c_lmka, &xo, &dpll, lmk05318_outs_cfg, 8, &ob->lmk, false);

    if (res) {
        USDR_LOG("XMSS", USDR_LOG_ERROR, "Unable to initialize XMASS\n");
    }

    usleep(10000); //wait until lmk digests all this

    //wait for PRIREF/SECREF validation
    res = lmk05318_wait_dpll_ref_stat(&ob->lmk, 4*60000000); //60s - searching for satellites may take a lot of time if GPS in just turned on
    if(res)
    {
        USDR_LOG("XMSS", USDR_LOG_ERROR, "LMK03518 DPLL input reference freqs are not validated during specified timeout");
        return res;
    }

    //wait for lock
    //APLL1/DPLL
    res = lmk05318_wait_apll1_lock(&ob->lmk, 100000);
    res = res ? res : lmk05318_wait_apll2_lock(&ob->lmk, 100000);

    unsigned los_msk;
    lmk05318_check_lock(&ob->lmk, &los_msk, false /*silent*/); //just to log state

    if(res)
    {
        USDR_LOG("XMSS", USDR_LOG_ERROR, "LMK03518 PLLs not locked during specified timeout");
        return res;
    }

    //sync to make APLL1/APLL2 & out channels in-phase
    //res = lmk05318_sync(&ob->lmk);
    //if(res)
    //    return res;

    res = (res) ? res : tca9555_reg16_set(dev, subdev, i2c_xraa, TCA9555_OUT0, (3) | (1 << 4) | (1 << 8) | (1 << 7) | (0 << 5));
    res = (res) ? res : tca9555_reg16_set(dev, subdev, i2c_xraa, TCA9555_OUT0, (3) | (1 << 4) | (1 << 8) | (1 << 7) | (1 << 5));
    USDR_LOG("XMSS", USDR_LOG_INFO, "LMK03518 outputs synced");

    ob->i2c_xraa = i2c_xraa;

    //res = (res) ? res : tca9555_reg16_set(dev, subdev, i2c_xraa, TCA9555_OUT0, (3) | (1 << 4) | (1 << 8) | (1 << 7) | (1 << 5) | (1 << 10));
    return res;
}



int board_xmass_ctrl_cmd_wr(board_xmass_t* ob, uint32_t addr, uint32_t reg)
{
    int res;

    switch (addr) {
    case P0:
    case P1:
        res = tca9555_reg8_set(ob->lmk.dev, ob->lmk.subdev, ob->i2c_xraa, TCA9555_OUT0 + addr - P0, reg);
        break;
    default:
        return -EINVAL;
    }

    // TODO update internal state!!!
    return res;
}

int board_xmass_ctrl_cmd_rd(board_xmass_t* ob, uint32_t addr, uint32_t* preg)
{
    uint8_t oval;
    int res;

    switch (addr) {
    case P0:
    case P1:
        res = tca9555_reg8_get(ob->lmk.dev, ob->lmk.subdev, ob->i2c_xraa, TCA9555_OUT0 + addr - P0, &oval);
        break;
    default:
        return -EINVAL;
    }

    *preg = oval;
    return res;
}

// 0 - off
// 1 - LO
// 2 - noise
// 3 - LO    - LNA3
// 4 - noise - LNA3

int board_xmass_sync_source(board_xmass_t* ob, unsigned sync_src)
{
    int res;
    unsigned default_cmd = (3) | (1 << 4) | (1 << 8) | (1 << 7) | (1 << 5);

    switch (sync_src) {
    case 0:
        res = tca9555_reg16_set(ob->lmk.dev, ob->lmk.subdev, ob->i2c_xraa, TCA9555_OUT0, default_cmd);
        break;
    case 2:
        res = tca9555_reg16_set(ob->lmk.dev, ob->lmk.subdev, ob->i2c_xraa, TCA9555_OUT0, default_cmd | (1 << XMASS_GPIO_RF_LB_SW) | (1 << XMASS_GPIO_RF_NOISE_EN) | (1 << XMASS_GPIO_RF_CAL_SRC_SEL));
        break;
    case 1:
        res = tca9555_reg16_set(ob->lmk.dev, ob->lmk.subdev, ob->i2c_xraa, TCA9555_OUT0, default_cmd | (1 << XMASS_GPIO_RF_LB_SW));
        break;
    case 4:
        res = tca9555_reg16_set(ob->lmk.dev, ob->lmk.subdev, ob->i2c_xraa, TCA9555_OUT0, default_cmd | (1 << XMASS_GPIO_RF_LB_SW) | (1 << XMASS_GPIO_RF_CAL_DST_SEL) | (1 << XMASS_GPIO_RF_NOISE_EN) | (1 << XMASS_GPIO_RF_CAL_SRC_SEL));
        break;
    case 3:
        res = tca9555_reg16_set(ob->lmk.dev, ob->lmk.subdev, ob->i2c_xraa, TCA9555_OUT0, default_cmd | (1 << XMASS_GPIO_RF_LB_SW) | (1 << XMASS_GPIO_RF_CAL_DST_SEL));
        break;
    default:
        return -EINVAL;
    }

    return res;
}


int board_xmass_tune_cal_lo(board_xmass_t* ob, uint32_t callo)
{
    int res = 0;
    unsigned los_msk;
    lmk05318_out_config_t lmk05318_outs_cfg[8];

    ob->calfreq = callo;

    res = res ? res : _board_xmass_fill_lmk05318(ob, lmk05318_outs_cfg);
    res = res ? res : lmk05318_solver(&ob->lmk, lmk05318_outs_cfg, 8);
    res = res ? res : lmk05318_reg_wr_from_map(&ob->lmk, false);

    res = res ? res : lmk05318_wait_apll1_lock(&ob->lmk, 10000);
    res = res ? res : lmk05318_wait_apll2_lock(&ob->lmk, 10000);
    //res = res ? res : lmk05318_sync(&ob->lmk);

    lmk05318_check_lock(&ob->lmk, &los_msk, false /*silent*/); //just to log state

    return res;
}






