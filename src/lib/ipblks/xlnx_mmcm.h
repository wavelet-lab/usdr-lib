// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef XLNX_MMCM_H
#define XLNX_MMCM_H

#include <usdr_logging.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>

// Configuration is in VCO cycles
struct mmcm_port_config_raw {
    int period_l;
    int period_h;
    int phase; // VCO phase
    int delay; // delay in digital cycles
};

enum {
    CLKOUT_PORT_5 = 0,
    CLKOUT_PORT_0 = 1,
    CLKOUT_PORT_1 = 2,
    CLKOUT_PORT_2 = 3,
    CLKOUT_PORT_3 = 4,
    CLKOUT_PORT_4 = 5,
    CLKOUT_PORT_6 = 6,
    CLKOUT_PORT_FB = 7,

    MAX_MMCM_PORTS = CLKOUT_PORT_FB + 1,
};

enum mmcm_type {
    MT_7SERIES_MMCM = 0,
    MT_7SERIES_PLLE2 = 1,
    MT_USP_MMCM = 2,
    MT_USP_PLLE3 = 3,
};

enum mmcm_vco_ranges {
    MMCM_VCO_MIN     =  600000000,
    MMCM_VCO_MAX     = 1440000000,
    MMCM_VCO_MAX_SP2 = 1900000000,
};

struct mmcm_config_raw {
    int type;
    struct mmcm_port_config_raw ports[MAX_MMCM_PORTS];
};

struct mmcm_port {
    unsigned freq;
    unsigned phase; // in deg
};

struct mmcm_config {
    unsigned infreq;
    struct mmcm_port ports[MAX_MMCM_PORTS - 1];
};

//int mmcm_calc_config(bool highspeed, const struct mmcm_config *in,
//                     struct mmcm_config_raw* rawout);

int mmcm_init_raw(lldev_t dev, subdev_t subdev, unsigned drp_port,
                  const struct mmcm_config_raw *config);


int mmcm_set_digdelay_raw(lldev_t dev, subdev_t subdev,
                          unsigned drp_port, unsigned port, unsigned delay);

int mmcm_set_phdigdelay_raw(lldev_t dev, subdev_t subdev,
                            unsigned drp_port, unsigned port, unsigned phdelay);

#endif
