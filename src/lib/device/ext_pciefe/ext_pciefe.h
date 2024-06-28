// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef EXT_PCIEFE_H
#define EXT_PCIEFE_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"


typedef int (*ext_i2c_func_t)(lldev_t dev, subdev_t subdev, unsigned ls_op, lsopaddr_t ls_op_addr,
                              size_t meminsz, void* pin, size_t memoutsz,
                              const void* pout);

enum board_type {
    V0_QORVO,
    V0_MINIC,
    V1_HALF,
    V1_FULL,
};
typedef enum board_type board_type_t;

enum rx_filters {
    RX_LPF1200 = 0,
    RX_LPF2100 = 1,
    RX_BPF2100_3000 = 2,
    RX_BPF3000_4200 = 3,
};
typedef enum rx_filters rx_filters_t;

enum tx_filters {
    TX_LPF400 = 0,
    TX_LPF1200 = 1,
    TX_LPF2100 = 2,
    TX_BYPASS = 3,
};
typedef enum tx_filters tx_filters_t;

enum trx_path {
    TRX_BYPASS = 0,
    TRX_BAND2 = 1,
    TRX_BAND3 = 2,
    TRX_BAND5 = 3,
    TRX_BAND7 = 4,
    TRX_BAND8 = 5,
};
typedef enum trx_path trx_path_t;


struct board_ext_pciefe {
    lldev_t dev;
    unsigned subdev;
    unsigned gpio_base;

    board_type_t board;
    ext_i2c_func_t func;

    // Configuration
    uint8_t cfg_fast_attn;
    uint8_t cfg_fast_lb;

    // Internal states
    uint8_t rxsel;
    uint8_t txsel;
    uint8_t trxsel;
    uint8_t trxloopback;

    uint8_t osc_en;
    uint8_t gps_en;

    uint8_t lna_en;
    uint8_t pa_en;

    uint8_t led;
    uint8_t rxattn;

    uint16_t dac;
};

typedef struct board_ext_pciefe board_ext_pciefe_t;

int board_ext_pciefe_init(lldev_t dev,
                          unsigned subdev,
                          unsigned gpio_base,
                          const char *params,
                          const char *compat,
                          ext_i2c_func_t func,
                          board_ext_pciefe_t* ob);

// Raw board interface
int board_ext_pciefe_ereg_wr(board_ext_pciefe_t* ob, uint32_t addr, uint32_t reg);
int board_ext_pciefe_ereg_rd(board_ext_pciefe_t* ob, uint32_t reg, uint32_t* preg);

// High level interface
int board_ext_pciefe_cmd_wr(board_ext_pciefe_t* ob, uint32_t addr, uint32_t reg);
int board_ext_pciefe_cmd_rd(board_ext_pciefe_t* ob, uint32_t reg, uint32_t* preg);


// Set best path for RXLO / TXLO and occupied bandwidth
int board_ext_pciefe_best_path_set(board_ext_pciefe_t* ob,
                                   unsigned rxlo, unsigned rxbw,
                                   unsigned txlo, unsigned txbw);



#endif
