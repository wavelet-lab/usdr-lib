// Copyright (c) 2023-2024 Wavelet Lab
//
// This work is dual-licensed under MIT and GPL 2.0.
// You can choose between one of them if you use this work.
//
// SPDX-License-Identifier: MIT OR GPL-2.0

#ifndef DEVICE_CORES_H
#define DEVICE_CORES_H

enum usdr_core_subtype {
    USDR_CS_INVALID = 0x0,

    // bus like spi, i2c, uart, ...
    USDR_CS_BUS = 0x1,

    // aux glue logic
    USDR_CS_AUX = 0x2,

    // streaming interface like pci, usb, ethernet, etc.
    USDR_CS_STREAM = 0x3,

    // front-end cores interfacing data hardware like JESD
    USDR_CS_FE = 0x4,

    // internal dsp cores, FFT, corr, FIR, CDC, etc.
    USDR_CS_DSP = 0x5,

    // ram and fifo interfaces
    USDR_CS_FIFORAM = 0x6,

    // internal MCU glue logic
    USDR_CS_MCU = 0x7,

    // non-standard user defined
    USDR_CS_USER = 0xff,

};

enum usdr_bus_cores {
    USDR_BS_SPI_SIMPLE = 0,
    USDR_BS_SPI_CFG_CS8 = 16,
    USDR_BS_DI2C_SIMPLE = 1,

    USDR_LIME_C64_PROTO = 2,
    USDR_DRP_PHY_V0 = 3,
    USDR_BS_UART_SIMPLE = 4,
    USDR_BS_GPIO15_SIMPLE = 5,

    USDR_BS_QSPIA24_R0 = 6,
};

enum usd_aux_cores {
    USDR_AC_PIC32_PCI = 0,
};

enum usdr_stream_cores {
    USDR_SC_RXDMA_BRSTN = 0,
    USDR_SC_TXDMA_OLD = 1,

    USDR_LIME_RX_STREAM = 2,
    USDR_LIME_TX_STREAM = 3,
};

enum usdr_fe_cores {
    USDR_FC_BRSTN = 0,
    USDR_TFE_BRSTN = 1,
};

#define USDR_MAKE_COREID(family, coreid) \
    (((family) & 0xff) | ((coreid) << 8))

#define USDR_CORE_GET_FAMILY(c) ((c) & 0xff)
#define USDR_CORE_GET_ID(c)     ((c) >> 8)


// Prdefined cores
#define I2C_CORE_AUTO_LUTUPD  USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_DI2C_SIMPLE)
#define SPI_CORE_32W          USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_SIMPLE)
#define SPI_CORE_CFGW_CS8     USDR_MAKE_COREID(USDR_CS_BUS, USDR_BS_SPI_CFG_CS8)


#endif
