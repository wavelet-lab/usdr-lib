// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef GENERIC_REGS_H
#define GENERIC_REGS_H

// Generic USDR register mmap

// Internal register layout (old layout)
enum REGS_generic_usdr_r000 {
    M2PCI_REG_STAT_CTRL = 0,
    M2PCI_REG_I2C = 1,
    M2PCI_REG_SPI0 = 2,

    // Either SPI1 or GPIO
    M2PCI_REG_SPI1 = 3,
    M2PCI_WR_GPIO_CB = 3,

    M2PCI_REG_QSPI_FLASH = 10,
    M2PCI_REG_INT = 15,

    M2PCI_REG_WR_RXDMA_CONFIRM = 4,
    M2PCI_REG_WR_RXDMA_CONTROL = 5,
    M2PCI_REG_WR_BADDR = 6,
    M2PCI_REG_WR_BDATA = 7,

    // Either TX or SPI 2/3
    M2PCI_REG_WR_TXDMA_CNF_L  = 12, //Burst len
    M2PCI_REG_WR_TXDMA_CNF_T  = 13, //Burst timing
    M2PCI_REG_WR_TXDMA_COMB   = 14, //Control

    M2PCI_REG_SPI2 = 13,
    M2PCI_REG_SPI3 = 14,

    M2PCI_REG_WR_LBDSP = 16,
    M2PCI_REG_RD_LBDSP = 12,

    M2PCI_REG_RD_FBUFFS = 4,
    M2PCI_REG_RD_FBURSTS = 5,
    M2PCI_REG_RD_RXSTAT = 6,
    M2PCI_REG_RD_AVGIDC = 7,
    M2PCI_REG_RD_AVGQDC = 8,

    REG_CFG_PHY_0       = 24,
    REG_CFG_PHY_1       = 25,

    M2PCI_REG_RD_TXDMA_STAT     = 28,
    M2PCI_REG_RD_TXDMA_STATM    = 29,
    M2PCI_REG_RD_TXDMA_STATTS   = 30,
    M2PCI_REG_RD_TXDMA_STAT_CPL = 31,
};

// New register layout



enum INTS_generic {
    M2PCI_INT_RX     = 0,
    M2PCI_INT_TX     = 1,
    M2PCI_INT_SPI_0  = 2,
    M2PCI_INT_SPI_1  = 3,
    M2PCI_INT_SPI_2  = 4,
    M2PCI_INT_SPI_3  = 5,
    M2PCI_INT_I2C_0  = 6,
};

enum VIRT_BUS_ADDRS {
    VIRT_CFG_SFX_BASE = 0x10000,
};

enum RFE_BUS_ADDRS {
    CSR_RFE4_BASE = VIRT_CFG_SFX_BASE + 256,
};

// All I2C devices used accross the famaly
enum {
    I2C_GENERAL_CALL  = 0x00,
    I2C_DEV_PMIC_FPGA = 0x60, //7'b110_0000;
    I2C_DEV_EXTDAC    = 0x48, //7'b100_1000;
    I2C_DEV_TMP114B   = 0x49, //7'b100_1001;
    I2C_DEV_TMP108A_A0_SDA   = 0x4A, //7'b100_1010;
    I2C_DEV_DAC80501M_A0_SCL = 0x4B, //7'b100_1011;
    I2C_DEV_TMP114NB  = 0x4E, //7'b100_1110;
    I2C_DEV_CLKGEN    = 0x6A, //7'b110_1010;
    I2C_DEV_DCDCBOOST = 0x75, //7'b111_0101;
};

#define MAKE_I2C_LUT(a,b,c,d) \
    (((a)<<24) | ((b)<<16) | ((c)<<8) | ((d)<<0))


enum igpi_generic {
    IGPI_USR_ACCESS2  = 0,
    IGPI_CORE_CONF1   = 4,
    IGPI_CORE_CONF2   = 8,
    IGPI_HWID        = 12,
};

enum {
    MASTER_IMAGE_OFF = 0x001c0000,
};


#endif
