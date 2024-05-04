// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_BUS_H
#define DEVICE_BUS_H

#include "device.h"

enum {
    DBMAX_SPI_BUSES = 4,
    DBMAX_I2C_BUSES = 4,
    DBMAX_IDXREG_MAPS = 2,
    DBMAX_SRX = 16,
    DBMAX_STX = 16,
    DBMAX_RFE = 8,
    DBMAX_TFE = 8,

    DBMAX_DRP = 8,
};

// Device BUSES information
struct device_bus
{
    unsigned spi_count;
    unsigned i2c_count;
    unsigned idx_regsps;
    unsigned srx_count;
    unsigned stx_count;
    unsigned rfe_count;
    unsigned tfe_count;
    unsigned drp_count;

    unsigned spi_base[DBMAX_SPI_BUSES];
    unsigned i2c_base[DBMAX_I2C_BUSES];
    unsigned idxreg_base[DBMAX_IDXREG_MAPS];
    unsigned idxreg_virt_base[DBMAX_IDXREG_MAPS];
    unsigned drp_base[DBMAX_DRP];

    unsigned srx_base[DBMAX_SRX];
    unsigned stx_base[DBMAX_STX];
    unsigned srx_cfg_base[DBMAX_SRX];
    unsigned stx_cfg_base[DBMAX_STX];
    unsigned srx_core[DBMAX_SRX];
    unsigned stx_core[DBMAX_STX];

    unsigned rfe_base[DBMAX_RFE];
    unsigned tfe_base[DBMAX_TFE];
    unsigned rfe_core[DBMAX_RFE];
    unsigned tfe_core[DBMAX_TFE];

    unsigned drp_core[DBMAX_DRP];

    int poll_event_rd;
    int poll_event_wr;
};

typedef struct device_bus device_bus_t;

int device_bus_init(pdevice_t dev, struct device_bus* pdb);

int device_bus_drp_generic_op(lldev_t dev, subdev_t subdev, const device_bus_t* db,
                              lsopaddr_t ls_op_addr,
                              size_t meminsz, void* pin,
                              size_t memoutsz, const void* pout);

#endif
