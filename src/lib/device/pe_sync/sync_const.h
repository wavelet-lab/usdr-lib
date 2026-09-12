#ifndef SYNC_CONST_H
#define SYNC_CONST_H

enum sync_base_regs {
    REG_WR_GPO            = 0,
    REG_RD_GPI            = 0,

    REG_SPI_EXT_CFG       = 1,
    REG_SPI_EXT_DATA      = 2,

    REG_GPIO_CTRL         = 3,

    REG_SPI0              = 4,
    REG_SPI1              = 5,
    REG_SPI2              = 6,
    REG_SPI3              = 7,

    REG_WR_PNTFY_CFG      = 8,
    REG_WR_PNTFY_ACK      = 9,

    REG_WR_FLASHSPI_CMD   = 10,
    REG_WR_FLASHSPI_ADDR  = 11,
    REG_RD_FLASHSPI_STAT  = 10,
    REG_RD_FLASHSPI_DATA  = 11,

    REG_SPI4              = 12,
    REG_SPI5              = 13,

    REG_UART_TRX          = 14,
    REG_INTS              = 15,

    REG_I2C0_CTRL         = 27,
    REG_I2C0_DATA         = 28,
    REG_I2C1_CTRL         = 29,
    REG_I2C1_DATA         = 30,
    REG_SPI6              = 31,
};

enum sync_base_ints {
    INT_SPI_0 = 0,
    INT_SPI_1 = 1,
    INT_SPI_2 = 2,
    INT_SPI_3 = 3,
    INT_SPI_4 = 4,
    INT_SPI_5 = 5,
    INT_SPI_6 = 6,
    INT_SPI_EXT = 7,
    INT_I2C_0 = 8,
    INT_I2C_1 = 9,
};

#endif
