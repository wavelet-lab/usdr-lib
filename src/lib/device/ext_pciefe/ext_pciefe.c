// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <string.h>
#include <usdr_logging.h>

#include "../ipblks/gpio.h"
#include "../ipblks/uart.h"
#include "../hw/dac80501/dac80501.h"
#include "../common/parse_params.h"

#include "ext_pciefe.h"

#include "def_pciefe.h"
#include "def_pciefe_cmd.h"

enum {
    GPIO_SDA     = GPIO0, // Alternatide mode
    GPIO_SCL     = GPIO1, // Alternatide mode

    GPIO_FATTN_0 = GPIO0, // Fast Attenuator interface
    GPIO_FATTN_1 = GPIO1, // Fast Attenuator interface

    GPIO_1PPS    = GPIO2,
    GPIO_UART_TX = GPIO3,
    GPIO_UART_RX = GPIO4,

    GPIO_FLBN    = GPIO5, // Fast TRX loopback interface

    // Serial RAM interface, DIR67, DIR89, DIRAB should be set to output in case of active SRAM
    SRAM_SCLK    = GPIO6,
    SRAM_CEN     = GPIO7,
    SRAM_SIO1    = GPIO8,
    SRAM_SIO2    = GPIO9,
    SRAM_SIO3    = GPIO10,
    SRAM_SIO0    = GPIO11,

    // High-speed GPIO groups
    // GPIO6,  GPIO7   -- DIR67
    // GPIO8,  GPIO9   -- DIR89
    // GPIO10, GPIO11  -- DIRAB
    // GPIO14, GPIO15  -- DIRCD
};

// GPIO Translatos
// DIRxx 0: PLD -> USDR  (usdr in)
// DIRxx 1: USDR -> PLD  (usdr out)

enum {
    VREG_FE = 0,
    VREG_GPIO = 1,
    VREG_DAC = 2,
};

enum {
    I2C_ADDR_FE = 0x20,
    I2C_ADDR_GPIO = 0x21,
    I2C_ADDR_DAC = 0x48,
};

enum {
    I2C_EXTERNAL_CMD_OFF = 16,
};

enum tca6416_regs {
    IN_P0,
    IN_P1,
    OUT_P0,
    OUT_P1,
    POL_INV_P0,
    POL_INV_P1,
    CONFIG_P0,
    CONFIG_P1, // 1 - input; 0 - output
};

int board_ext_pciefe_updpwr(board_ext_pciefe_t* ob);
int board_ext_pciefe_updfe(board_ext_pciefe_t* ob);


static int tca6416_reg_wr(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                   uint8_t reg, uint8_t out)
{
    uint8_t data[2] = { reg, out };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          0, NULL, SIZEOF_ARRAY(data), data);
}

static int tca6416_reg_wr2(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                    uint8_t reg, uint8_t out0, uint8_t out1)
{
    uint8_t data[3] = { reg, out0, out1 };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          0, NULL, SIZEOF_ARRAY(data), data);
}

static int tca6416_reg_rd(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                          uint8_t addr, uint8_t* val)
{
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          1, val, 1, &addr);
}

static int tca6416_reg_rd4(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                           uint8_t addr, uint32_t* val)
{
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          4, (uint8_t*)val, 1, &addr);
}

static inline int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}

static int _board_ext_pciefe_wr(board_ext_pciefe_t* ob, uint32_t v)
{
    return board_ext_pciefe_ereg_wr(ob, v >> 16, v);
}

enum {
    EN_PA = 1,
    EN_LNA = 2,
};

struct path_map {
    const char* path;
    unsigned dupl;
    unsigned rx;
    unsigned tx;
    unsigned flags;
};

static const struct path_map known_path[] = {
    // Duplexers path
    { "band2", TRX_BAND2, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },
    { "pcs", TRX_BAND2, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },
    { "gsm1900", TRX_BAND2, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },

    { "band3", TRX_BAND3, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },
    { "dcs", TRX_BAND3, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },
    { "gsm1800", TRX_BAND3, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },

    { "band5", TRX_BAND5, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },
    { "gsm850", TRX_BAND5, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },

    { "band7", TRX_BAND7, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },
    { "imte", TRX_BAND7, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },

    { "band8", TRX_BAND8, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },
    { "gsm900", TRX_BAND8, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },

    // TX-only path
    { "txlpf400", TRX_BYPASS, RX_LPF1200, TX_LPF400, EN_PA },
    { "txlpf1200", TRX_BYPASS, RX_LPF1200, TX_LPF1200, EN_PA },
    { "txlpf2100", TRX_BYPASS, RX_LPF1200, TX_LPF2100, EN_PA },
    { "txlpf4200", TRX_BYPASS, RX_LPF1200, TX_BYPASS, EN_PA },

    // RX-only path
    { "rxlpf1200", TRX_BYPASS, RX_LPF1200, TX_LPF400, EN_LNA },
    { "rxlpf2100", TRX_BYPASS, RX_LPF2100, TX_LPF400, EN_LNA },
    { "rxbpf2100_3000", TRX_BYPASS, RX_BPF2100_3000, TX_LPF400, EN_LNA },
    { "rxbpf3000_4200", TRX_BYPASS, RX_BPF3000_4200, TX_LPF400, EN_LNA },

    // TDD / half duplex modes
    { "trx0_400", TRX_BYPASS, RX_LPF1200, TX_LPF400, EN_PA | EN_LNA },
    { "trx400_1200", TRX_BYPASS, RX_LPF1200, TX_LPF1200, EN_PA | EN_LNA },
    { "trx1200_2100", TRX_BYPASS, RX_LPF2100, TX_LPF2100, EN_PA | EN_LNA },
    { "trx2100_3000", TRX_BYPASS, RX_BPF2100_3000, TX_BYPASS, EN_PA | EN_LNA },
    { "trx3000_4200", TRX_BYPASS, RX_BPF3000_4200, TX_BYPASS, EN_PA | EN_LNA },
};

int board_ext_pciefe_init(lldev_t dev,
                          unsigned subdev,
                          unsigned gpio_base,
                          unsigned uart_base,
                          const char* params,
                          const char* compat,
                          unsigned i2c_loc,
                          board_ext_pciefe_t* ob)
{
    int res = 0;
    uint32_t dummy;
    long dac_val = 0, p_attn;
    unsigned i2ca_gpio = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(i2c_loc), LSOP_I2C_BUSNO(i2c_loc), I2C_ADDR_GPIO);
    unsigned i2ca_fe = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(i2c_loc), LSOP_I2C_BUSNO(i2c_loc), I2C_ADDR_FE);
    unsigned i2ca_dac = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(i2c_loc), LSOP_I2C_BUSNO(i2c_loc), I2C_ADDR_DAC);


    // This breakout is compatible with M.2 key A/E or A+E boards
    if ((strcmp(compat, "m2a+e") != 0) && (strcmp(compat, "m2e") != 0) && (strcmp(compat, "m2a") != 0))
        return -ENODEV;

    // Configure external SDA/SCL
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SDA, GPIO_CFG_IN);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SCL, GPIO_CFG_IN);

    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_1PPS, GPIO_CFG_ALT0);
    if (res)
        return res;

    ob->dev = dev;
    ob->subdev = subdev;
    ob->gpio_base = gpio_base;
    ob->i2c_loc = i2c_loc;
    ob->board = V1_HALF;

    ob->rxsel = RX_LPF1200;
    ob->txsel = TX_BYPASS;
    ob->trxsel = TRX_BYPASS;
    ob->trxloopback = 0;

    // We need to enable VOSC to check DAC ID
    ob->osc_en = 1;
    ob->gps_en = 0;

    ob->lna_en = 0;
    ob->pa_en = 0;

    ob->led = 0;
    ob->rxattn = 0;

    ob->cfg_fast_attn = 0;
    ob->cfg_fast_lb = 0;

    enum { P_PATH, P_GPS, P_OSC, P_LNA, P_PA, P_DAC, P_LB, P_UART, P_ATTN };
    static const char* ppars[] = {
        "path_",
        "gps_",
        "osc_",
        "lna_",
        "pa_",
        "dac_",
        "lb_",
        "uart_",
        "attn_",
        "v0",
        "v0a",
        "v1",
        NULL,
    };
    struct param_data pd[SIZEOF_ARRAY(ppars)];
    memset(pd, 0, sizeof(pd));

    if (params != 0) {
        const char* fault = NULL;
        parse_params(params, ':', ppars, pd, &fault);
        if (fault) {
            USDR_LOG("M2PE", USDR_LOG_ERROR, "Unrecognized front end option: `%s`\n", fault);
            return -EINVAL;
        }

        if (strncmp(params, "v0", 2) == 0) {
            ob->board = V0_QORVO;
        } else if (strncmp(params, "v0a", 3) == 0) {
            ob->board = V0_MINIC;
        } else {
            if (strncmp(params, "v1", 2) != 0) {
                USDR_LOG("PCIF", USDR_LOG_WARNING, "Requested unknown board revision: `%s`, defaulting to V1\n", params);
            }

            ob->board = V1_HALF;
        }
    }

    if (ob->board == V1_HALF || ob->board == V1_FULL) {
        ob->cfg_fast_attn = 1;
        ob->cfg_fast_lb = 1;
    }

    unsigned exp_addrs[] = {i2ca_fe, i2ca_gpio};
    for (unsigned i = 0; i < 2; i++) {
        res = tca6416_reg_rd4(dev, subdev, exp_addrs[i], IN_P1, &dummy);
        if (res)
            return res;
        if (dummy == 0xbaadbeef) {
            USDR_LOG("PCIF",
                     (params == NULL) ? USDR_LOG_INFO : USDR_LOG_ERROR,
                     "Unable to initialize expander %d\n", i);
            return (params == NULL) ? -ENODEV : -EIO;
        }
    }

    res = (res) ? res : tca6416_reg_wr(dev, subdev, i2ca_gpio, CONFIG_P0, 0);
    res = (res) ? res : tca6416_reg_wr(dev, subdev, i2ca_gpio, CONFIG_P1, 0);
    res = (res) ? res : tca6416_reg_wr(dev, subdev, i2ca_gpio, OUT_P0, 0);
    res = (res) ? res : tca6416_reg_wr(dev, subdev, i2ca_gpio, OUT_P1, 0);

    res = (res) ? res : tca6416_reg_wr(dev, subdev, i2ca_fe, CONFIG_P0, 0);
    res = (res) ? res : tca6416_reg_wr(dev, subdev, i2ca_fe, CONFIG_P1, 0);
    res = (res) ? res : tca6416_reg_wr(dev, subdev, i2ca_fe, OUT_P0, 0);
    res = (res) ? res : tca6416_reg_wr(dev, subdev, i2ca_fe, OUT_P1, 1);

    res = (res) ? res : board_ext_pciefe_updpwr(ob);
    if (res)
        return res;

    uint16_t rdreg[2];
    for (unsigned i = 0; i < 2; i++) {
        res = tca6416_reg_rd4(dev, subdev, exp_addrs[i], IN_P0, &dummy);
        if (res)
            return res;

        rdreg[i] = dummy;
    }
    if (rdreg[0] == 0x0000 || rdreg[1] == 0x0000) {
        USDR_LOG("PCIF",
                 (params == NULL) ? USDR_LOG_WARNING : USDR_LOG_ERROR,
                 "FE/GPIO expander tca6416 is dead! (FE=%04x GPIO=%04x)\n", rdreg[0], rdreg[1]);
        return (params == NULL) ? -ENODEV : -EIO;
    }

    // Wait for power up DAC
    usleep(10000);

    res = dac80501_init(dev, subdev, i2ca_dac, DAC80501_CFG_REF_DIV_GAIN_MUL);
    if (res) {
        USDR_LOG("PCIF", USDR_LOG_ERROR, "DAC initialization error=%d\n", res);
        if (ob->board == V1_HALF || ob->board == V1_FULL) {
            return -EIO;
        }
    }

    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_1PPS, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_UART_TX, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_UART_RX, GPIO_CFG_ALT0);
    if (res)
        return res;

    ob->osc_en = 0;

    // Parse parameters
    if (pd[P_PATH].item_len) {
        for (unsigned j = 0; j < SIZEOF_ARRAY(known_path); j++) {
            size_t l = strlen(known_path[j].path);
            if (pd[P_PATH].item_len != l)
                continue;

            if (strncmp(pd[P_PATH].item, known_path[j].path, l) == 0) {
                const struct path_map *c = &known_path[j];
                USDR_LOG("PCIF", USDR_LOG_NOTE, "Found `%s` path!\n", c->path);

                ob->txsel = c->tx;
                ob->rxsel = c->rx;
                ob->trxsel = c->dupl;
                ob->pa_en = (c->flags & EN_PA) ? 1 : 0;
                ob->lna_en = (c->flags & EN_LNA) ? 1 : 0;
                goto found_path;
            }
        }
        USDR_LOG("PCIF", USDR_LOG_WARNING, "Front end path `%*.*s` not found!\n", (int)pd[P_PATH].item_len, (int)pd[P_PATH].item_len, pd[P_PATH].item);
        for (unsigned q = 0; q < SIZEOF_ARRAY(known_path); q++) {
            USDR_LOG("PCIF", USDR_LOG_INFO, " path %*.*s: LNA=%d PA=%d\n", 16, 16,
                     known_path[q].path, known_path[q].flags & EN_LNA ? 1 : 0, known_path[q].flags & EN_PA ? 1 : 0);
        }
    found_path:;
    }
    if (((res = is_param_on(&pd[P_GPS]))) >= 0) {
        ob->gps_en = res;
    }
    if (((res = is_param_on(&pd[P_OSC]))) >= 0) {
        ob->osc_en = res;
    }
    if (((res = is_param_on(&pd[P_LNA]))) >= 0) {
        ob->lna_en = res;
    }
    if (((res = is_param_on(&pd[P_PA]))) >= 0) {
        ob->pa_en = res;
    }
    if (((res = is_param_on(&pd[P_LB]))) >= 0) {
        ob->trxloopback = res;
    }
    if (get_param_long(&pd[P_ATTN], &p_attn) == 0) {
        ob->rxattn = (p_attn + 3) / 6;
        if (ob->rxattn > 3)
            ob->rxattn = 3;
    }
    if (get_param_long(&pd[P_DAC], &dac_val) == 0) {
        if ((dac_val < 0) || (dac_val > 65535)) {
            USDR_LOG("PCIF", USDR_LOG_ERROR, "DAC value must be in range [0;65535]\n");
            return -EINVAL;
        }

        res = board_ext_pciefe_cmd_wr(ob, FECMD_DAC, dac_val);
        if (res)
            return res;
    }

    res = 0;
    if (ob->cfg_fast_attn) {
        res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_FATTN_0, GPIO_CFG_OUT);
        res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_FATTN_1, GPIO_CFG_OUT);
        res = (res) ? res : gpio_cmd(ob->dev, ob->subdev, ob->gpio_base, GPIO_OUT,
                                     (1 << GPIO_FATTN_1) | (1 << GPIO_FATTN_0),
                                     (((ob->rxattn >> 0) & 1) << GPIO_FATTN_1) | (((ob->rxattn >> 1) & 1) << GPIO_FATTN_0));
    }
    if (ob->cfg_fast_lb) {
        res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_FLBN, GPIO_CFG_OUT);
        res = (res) ? res : gpio_cmd(ob->dev, ob->subdev, ob->gpio_base, GPIO_OUT,
                                     1 << GPIO_FLBN,
                                     (ob->trxloopback ? 0 : 1) << GPIO_FLBN);
    }

    res = res ? res : board_ext_pciefe_updfe(ob);
    res = res ? res : board_ext_pciefe_updpwr(ob);

    if (is_param_on(&pd[P_UART]) == 1) {
        // check uart
        char b[2048];
        uart_core_t uc;
        res = (res) ? res : uart_core_init(dev, subdev, uart_base, &uc);
        res = (res) ? res : uart_core_rx_collect(&uc, sizeof(b), b, 2250);

        if (res > 0)
            res = 0;
        USDR_LOG("M2PE", USDR_LOG_ERROR, "UART: `%s`\n", b);
    }
    if (res)
        return res;

    USDR_LOG("PCIF", USDR_LOG_INFO, "PCIeFE initialized, mod %d\n", ob->board);
    return 0;
}

int board_ext_pciefe_updpwr(board_ext_pciefe_t* ob)
{
    uint32_t regs[2];
    int leds[4] = {
        (ob->led >> 0) & 1,
        (ob->led >> 1) & 1,
        (ob->led >> 2) & 1,
        (ob->led >> 3) & 1,
    };
    int res = 0;
    USDR_LOG("PCIF", USDR_LOG_INFO, "PCIeFE GPS=%d OSC=%d LED=[%d%d%d%d]\n",
             ob->gps_en, ob->osc_en,
             leds[3], leds[2], leds[1], leds[0]);

    switch (ob->board) {
    case V0_QORVO:
    case V0_MINIC:
        regs[0] = MAKE_PCIEFE_V0_GPIO0(1, 1,
                                       0, 0, 0, 0,
                                       ob->gps_en, ob->osc_en);
        regs[1] = MAKE_PCIEFE_V0_GPIO1(leds[3], leds[2], leds[1], leds[0],
                                       1, 1, 1, 1);
        break;
    case V1_HALF:
    case V1_FULL:
        regs[0] = MAKE_PCIEFE_V1_GPIO0(1, 1, 1, 1,
                                       leds[1], leds[0],
                                       ob->gps_en, ob->osc_en);
        regs[1] = MAKE_PCIEFE_V1_GPIO1(leds[3], leds[2],
                                       0, 0, 0, 0, 0, 0);
        break;
    default:
        return -EINVAL;
    }

    res = (res) ? res : _board_ext_pciefe_wr(ob, regs[0]);
    res = (res) ? res : _board_ext_pciefe_wr(ob, regs[1]);
    return res;
}

int board_ext_pciefe_updfe(board_ext_pciefe_t* ob)
{
    uint32_t regs[2] = {0, 0};
    int res = 0;

    static const char* name_rx_p[] = { "lpf1200", "lpf2100", "bpf2100_3000", "bpf3000_4200" };
    static const char* name_tx_p[] = { "lpf400", "lpf1200", "lpf2100", "lpf4200" };
    static const char* name_trx_p[] = { "bypass", "band2", "band3", "band5", "band7", "band8" };

    USDR_LOG("PCIF", USDR_LOG_INFO, "PCIeFE RX=%s TX=%s TRX=%s LNA=%d PA=%d ATTN=%d LB=%d\n",
             name_rx_p[ob->rxsel], name_tx_p[ob->txsel], name_trx_p[ob->trxsel], ob->lna_en, ob->pa_en, ob->rxattn, ob->trxloopback);

    switch (ob->board) {
    case V0_QORVO:
        regs[0] = MAKE_PCIEFE_V0_FE0(
            ob->txsel == TX_LPF400 ? V0_FE0_TXSEL2_RF2_LPF_400 :
                ob->txsel == TX_LPF1200 ? V0_FE0_TXSEL2_RF4_LPF_1200 :
                ob->txsel == TX_LPF2100 ? V0_FE0_TXSEL2_RF3_LPF_2100 : V0_FE0_TXSEL2_RF1_BYPASS,
            ob->rxsel == RX_LPF1200 ? V0_FE0_RXSEL2_RF2_LPF_1200 :
                ob->rxsel == RX_LPF2100 ? V0_FE0_RXSEL2_RF1_LPF_2100 :
                ob->rxsel == RX_BPF2100_3000 ? V0_FE0_RXSEL2_RF3_BPF_2100_3000 : V0_FE0_RXSEL2_RF4_BPF_3000_4200,
            ob->rxsel == RX_LPF1200 ? V0_FE0_RXSEL1_RF4_LPF_1200 :
                ob->rxsel == RX_LPF2100 ? V0_FE0_RXSEL1_RF3_LPF_2100 :
                ob->rxsel == RX_BPF2100_3000 ? V0_FE0_RXSEL1_RF1_BPF_2100_3000 : V0_FE0_RXSEL1_RF2_BPF_3000_4200,
            ob->txsel == TX_LPF400 ? V0_FE0_TXSEL1_RF4_LPF_400 :
                ob->txsel == TX_LPF1200 ? V0_FE0_TXSEL1_RF2_LPF_1200 :
                ob->txsel == TX_LPF2100 ? V0_FE0_TXSEL1_RF1_LPF_2100 : V0_FE0_TXSEL1_RF3_BYPASS
            );
        regs[1] = MAKE_PCIEFE_V0_FE1(
            ob->lna_en ? 1 : 0, (ob->pa_en || ob->lna_en) ? 1 : 0,
            ob->rxattn == 0 ? V0_FE1_ATTN_IL :
                ob->rxattn == 1 ? V0_FE1_ATTN_6DB :
                ob->rxattn == 2 ? V0_FE1_ATTN_12DB : V0_FE1_ATTN_18DB,
            ob->trxloopback ? 0 : 1,
            ob->trxsel == TRX_BAND2 ? V0_FE1_DUPL_PATH_RF3_BAND2 :
                ob->trxsel == TRX_BAND3 ? V0_FE1_DUPL_PATH_RF5_BAND3 :
                ob->trxsel == TRX_BAND5 ? V0_FE1_DUPL_PATH_RF1_BAND5 :
                ob->trxsel == TRX_BAND7 ? V0_FE1_DUPL_PATH_RF6_BAND7 :
                ob->trxsel == TRX_BAND8 ? V0_FE1_DUPL_PATH_RF2_BAND8 : V0_FE1_DUPL_PATH_RF4_BYPASS
            );
        break;
    case V0_MINIC:
        regs[0] = MAKE_PCIEFE_V0_FE0_ALT(
            ob->txsel == TX_LPF400 ? V0_FE0_ALT_ATXSEL2_ARF1_LPF_400 :
                ob->txsel == TX_LPF1200 ? V0_FE0_ALT_ATXSEL2_ARF2_LPF_1200 :
                ob->txsel == TX_LPF2100 ? V0_FE0_ALT_ATXSEL2_ARF4_LPF_2100 : V0_FE0_ALT_ATXSEL2_ARF3_BYPASS,
            ob->rxsel == RX_LPF1200 ? V0_FE0_ALT_ARXSEL2_ARF1_LPF_1200 :
                ob->rxsel == RX_LPF2100 ? V0_FE0_ALT_ARXSEL2_ARF3_LPF_2100 :
                ob->rxsel == RX_BPF2100_3000 ? V0_FE0_ALT_ARXSEL2_ARF4_BPF_2100_3000 : V0_FE0_ALT_ARXSEL2_ARF2_BPF_3000_4200,
            ob->rxsel == RX_LPF1200 ? V0_FE0_ALT_ARXSEL1_ARF2_LPF_1200 :
                ob->rxsel == RX_LPF2100 ? V0_FE0_ALT_ARXSEL1_ARF3_LPF_2100 :
                ob->rxsel == RX_BPF2100_3000 ? V0_FE0_ALT_ARXSEL1_ARF3_BPF_2100_3000 : V0_FE0_ALT_ARXSEL1_ARF1_BPF_3000_4200,
            ob->txsel == TX_LPF400 ? V0_FE0_ALT_ATXSEL1_ARF2_LPF_400 :
                ob->txsel == TX_LPF1200 ? V0_FE0_ALT_ATXSEL1_ARF1_LPF_1200 :
                ob->txsel == TX_LPF2100 ? V0_FE0_ALT_ATXSEL1_ARF3_LPF_2100 : V0_FE0_ALT_ATXSEL1_ARF4_BYPASS
            );
        regs[1] = MAKE_PCIEFE_V0_AFE1_ALT(
            ob->lna_en ? 1 : 0, ob->pa_en ? 1 : 0,
            ob->rxattn == 0 ? V0_AFE1_ALT_AATTN_IL :
                ob->rxattn == 1 ? V0_AFE1_ALT_AATTN_6DB :
                ob->rxattn == 2 ? V0_AFE1_ALT_AATTN_12DB : V0_AFE1_ALT_AATTN_18DB,
            ob->trxloopback ? 0 : 1,
            ob->trxsel == TRX_BAND2 ? V0_AFE1_ALT_ADUPL_PATH_ARF1_BAND2 :
                ob->trxsel == TRX_BAND3 ? V0_AFE1_ALT_ADUPL_PATH_ARF4_BAND3 :
                ob->trxsel == TRX_BAND5 ? V0_AFE1_ALT_ADUPL_PATH_ARF5_BAND5 :
                ob->trxsel == TRX_BAND7 ? V0_AFE1_ALT_ADUPL_PATH_ARF2_BAND7 :
                ob->trxsel == TRX_BAND8 ? V0_AFE1_ALT_ADUPL_PATH_ARF3_BAND8 : V0_AFE1_ALT_ADUPL_PATH_ARF6_BYPASS
            );
        break;
    case V1_HALF:
    case V1_FULL:
        regs[0] = MAKE_PCIEFE_V1_FE(
            ob->rxattn == 0 ? V1_FE_ATTN_IL :
                ob->rxattn == 1 ? V1_FE_ATTN_6DB :
                ob->rxattn == 2 ? V1_FE_ATTN_12DB : V1_FE_ATTN_18DB,
            (ob->osc_en) ? 0 : 1,
            (ob->pa_en) ? 0 : 1,
            (ob->lna_en) ? 0 : 1,
            (ob->lna_en) ? 0 : 1,
            ob->trxsel == TRX_BAND2 ? V1_FE_DUPL_PATH_RF3_BAND2 :
                ob->trxsel == TRX_BAND3 ? V1_FE_DUPL_PATH_RF5_BAND3 :
                ob->trxsel == TRX_BAND5 ? V1_FE_DUPL_PATH_RF1_BAND5 :
                ob->trxsel == TRX_BAND7 ? V1_FE_DUPL_PATH_RF6_BAND7 :
                ob->trxsel == TRX_BAND8 ? V1_FE_DUPL_PATH_RF2_BAND8 : V1_FE_DUPL_PATH_RF4_BYPASS,
            (ob->pa_en) ? 0 : 1,
            ob->rxsel == RX_LPF1200 ? V1_FE_RXSEL_LPF_1200_LPF_1200 :
                ob->rxsel == RX_LPF2100 ? V1_FE_RXSEL_LPF_2100_LPF_2100 :
                ob->rxsel == RX_BPF2100_3000 ? V1_FE_RXSEL_BPF_2100_3000_BPF_2100_3000 : V1_FE_RXSEL_BPF_3000_4200_BPF_3000_4200,
            ob->txsel == TX_LPF400 ? V1_FE_TXSEL_LPF_400_LPF_400 :
                ob->txsel == TX_LPF1200 ? V1_FE_TXSEL_LPF_1200_LPF_1200 :
                ob->txsel == TX_LPF2100 ? V1_FE_TXSEL_LPF_2100_LPF_2100 : V1_FE_TXSEL_LPF_4000_LPF_4000
            );
        break;
    default:
        return -EINVAL;
    }

    res = (res || (regs[0] == 0)) ? res : _board_ext_pciefe_wr(ob, regs[0]);
    res = (res || (regs[1] == 0)) ? res : _board_ext_pciefe_wr(ob, regs[1]);
    return res;
}


int board_ext_pciefe_ereg_wr(board_ext_pciefe_t* ob, uint32_t addr, uint32_t reg)
{
    int res;
    unsigned i2ca_gpio = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(ob->i2c_loc), LSOP_I2C_BUSNO(ob->i2c_loc), I2C_ADDR_GPIO);
    unsigned i2ca_fe = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(ob->i2c_loc), LSOP_I2C_BUSNO(ob->i2c_loc), I2C_ADDR_FE);
    unsigned i2ca_dac = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(ob->i2c_loc), LSOP_I2C_BUSNO(ob->i2c_loc), I2C_ADDR_DAC);

    switch (addr) {
    case V1_FE:
        res = tca6416_reg_wr2(ob->dev, ob->subdev, i2ca_fe, OUT_P0, reg, reg >> 8);
        break;
    case V0_FE0:
    case V0_FE0_ALT:
        res = tca6416_reg_wr(ob->dev, ob->subdev, i2ca_fe, OUT_P0, reg);
        break;
    case V0_FE1:
    case V0_AFE1_ALT:
        res = tca6416_reg_wr(ob->dev, ob->subdev, i2ca_fe, OUT_P1, reg);
        break;
    case V0_GPIO0:
    case V1_GPIO0:
        res = tca6416_reg_wr(ob->dev, ob->subdev, i2ca_gpio, OUT_P0, reg);
        break;
    case V0_GPIO1:
    case V1_GPIO1:
        res = tca6416_reg_wr(ob->dev, ob->subdev, i2ca_gpio, OUT_P1, reg);
        break;
    case GENERAL_DAC:
        res = dac80501_dac_set(ob->dev, ob->subdev, i2ca_dac, reg);
        break;
    default:
        return -EINVAL;
    }

    return res;
}

int board_ext_pciefe_ereg_rd(board_ext_pciefe_t* ob, uint32_t addr, uint32_t* preg)
{
    int res;
    uint8_t val = 0xff;
    unsigned i2ca_gpio = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(ob->i2c_loc), LSOP_I2C_BUSNO(ob->i2c_loc), I2C_ADDR_GPIO);
    unsigned i2ca_fe = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(ob->i2c_loc), LSOP_I2C_BUSNO(ob->i2c_loc), I2C_ADDR_FE);
    unsigned i2ca_dac = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(ob->i2c_loc), LSOP_I2C_BUSNO(ob->i2c_loc), I2C_ADDR_DAC);

    switch (addr) {
    case GENERAL_DAC: {
        uint16_t dreg = 0;
        res = dac80501_dac_get(ob->dev, ob->subdev, i2ca_dac, &dreg);
        *preg = dreg;
        return res;
    }
    case V1_FE: {
        uint8_t r0, r1;
        res = tca6416_reg_rd(ob->dev, ob->subdev, i2ca_fe, OUT_P0, &r0);
        if (res)
            return res;
        res = tca6416_reg_rd(ob->dev, ob->subdev, i2ca_fe, OUT_P1, &r1);
        if (res)
            return res;

        *preg = ((uint32_t)r1 << 8) | r0;
        return res;
    }
    case V0_FE0:
    case V0_FE0_ALT:
        res = tca6416_reg_rd(ob->dev, ob->subdev, i2ca_fe, OUT_P0, &val);
        break;
    case V0_FE1:
    case V0_AFE1_ALT:
        res = tca6416_reg_rd(ob->dev, ob->subdev, i2ca_fe, OUT_P1, &val);
        break;
    case V0_GPIO0:
    case V1_GPIO0:
        res = tca6416_reg_rd(ob->dev, ob->subdev, i2ca_gpio, OUT_P0, &val);
        break;
    case V0_GPIO1:
    case V1_GPIO1:
        res = tca6416_reg_rd(ob->dev, ob->subdev, i2ca_gpio, OUT_P1, &val);
        break;
    default:
        return -EINVAL;
    }
    *preg = val;
    return res;
}

int board_ext_pciefe_cmd_wr(board_ext_pciefe_t* ob, uint32_t addr, uint32_t reg)
{
    int res = 0;
    unsigned i2ca_dac = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(ob->i2c_loc), LSOP_I2C_BUSNO(ob->i2c_loc), I2C_ADDR_DAC);

    switch (addr) {
    case FECMD_TXSEL: ob->txsel = reg; break;
    case FECMD_RXSEL: ob->rxsel = reg; break;
    case FECMD_DUPLSEL: ob->trxsel = reg; break;
    case FECMD_CTRL_LNA: ob->lna_en = reg; break;
    case FECMD_CTRL_PA: ob->pa_en = reg; break;
    case FECMD_GPS: ob->gps_en = reg; break;
    case FECMD_OSC: ob->osc_en = reg; break;
    case FECMD_LED: ob->led = reg; break;
    case FECMD_LOOPBACK:
        ob->trxloopback = reg;
        if (ob->cfg_fast_lb) {
            return gpio_cmd(ob->dev, ob->subdev, ob->gpio_base, GPIO_OUT,
                            1 << GPIO_FLBN,
                            (ob->trxloopback ? 0 : 1) << GPIO_FLBN);
        }
        break;
    case FECMD_ATTN:
        ob->rxattn = reg;
        if (ob->cfg_fast_attn) {
            return gpio_cmd(ob->dev, ob->subdev, ob->gpio_base, GPIO_OUT,
                            (1 << GPIO_FATTN_1) | (1 << GPIO_FATTN_0),
                            (((ob->rxattn >> 0) & 1) << GPIO_FATTN_1) | (((ob->rxattn >> 1) & 1) << GPIO_FATTN_0));
        }
        break;
    case FECMD_DAC:
        ob->dac = reg;
        return dac80501_dac_set(ob->dev, ob->subdev, i2ca_dac, reg);
    default:
        return -EINVAL;
    }

    res = (res) ? res : board_ext_pciefe_updfe(ob);
    res = (res) ? res : board_ext_pciefe_updpwr(ob);
    return res;
}

int board_ext_pciefe_cmd_rd(board_ext_pciefe_t* ob, uint32_t addr, uint32_t* preg)
{
    uint32_t ret = ~0u;

    switch (addr) {
    case FECMD_DAC: ret = ob->dac; break;
    case FECMD_TXSEL: ret = ob->txsel; break;
    case FECMD_RXSEL: ret = ob->rxsel; break;
    case FECMD_DUPLSEL: ret = ob->trxsel; break;
    case FECMD_LOOPBACK: ret = ob->trxloopback; break;
    case FECMD_ATTN: ret = ob->rxattn; break;
    case FECMD_CTRL_LNA: ret = ob->lna_en; break;
    case FECMD_CTRL_PA: ret = ob->pa_en; break;
    case FECMD_GPS: ret = ob->gps_en; break;
    case FECMD_OSC: ret = ob->osc_en; break;
    case FECMD_LED: ret = ob->led; break;
    default:
        return -EINVAL;
    }

    *preg = ret;
    return 0;
}


struct band_selector {
    unsigned start;
    unsigned stop;
};

struct dupl_selector {
    struct band_selector ul;
    struct band_selector dl;
};

// Set best path for RXLO / TXLO and occupied bandwidth
int board_ext_pciefe_best_path_set(board_ext_pciefe_t* ob,
                                   unsigned rxlo, unsigned rxbw,
                                   unsigned txlo, unsigned txbw)
{

    return -EINVAL;
}

int board_ext_pciefe_set_dac(board_ext_pciefe_t* brd, unsigned value)
{
    unsigned i2ca_dac = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(brd->i2c_loc), LSOP_I2C_BUSNO(brd->i2c_loc), I2C_ADDR_DAC);

    brd->dac = value;
    return dac80501_dac_set(brd->dev, brd->subdev, i2ca_dac, brd->dac);
}
