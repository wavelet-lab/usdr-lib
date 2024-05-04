// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_NAMES_H
#define DEVICE_NAMES_H

// Standard parameter names in device vfs

#define DNLL_PREFIX_NAME    "/ll/"
#define DNDM_PREFIX_NAME    "/dm/"


#define DNP_COUNT           "count"
#define DNP_CORE            "core"
#define DNP_BASE            "base"
#define DNP_CFG_BASE        "cfg_base"
#define DNP_VIRT_BASE       "virt_base"
#define DNP_IRQ             "irq"

#define DNP_DMACAP          "dmacap"
#define DNP_FIFOBSZ         "fifobsz"

#define DN_BUS_SPI          "spi"
#define DN_BUS_I2C          "i2c"
#define DN_IDX_REGSP        "idx_regsp"
#define DN_SRX              "srx"
#define DN_STX              "stx"
#define DN_RFE              "rfe"
#define DN_TFE              "tfe"
#define DN_DRP              "drp"

#define DNLL(x)  DNLL_PREFIX_NAME x
#define DNLLC(x) DNLL_PREFIX_NAME x "_" DNP_COUNT

#define DNLL_SPI_COUNT DNLLC(DN_BUS_SPI)
#define DNLL_I2C_COUNT DNLLC(DN_BUS_I2C)
#define DNLL_IDX_REGSP_COUNT DNLLC(DN_IDX_REGSP)
#define DNLL_IRQ_COUNT DNLLC(DNP_IRQ)
#define DNLL_SRX_COUNT DNLLC(DN_SRX)
#define DNLL_STX_COUNT DNLLC(DN_STX)
#define DNLL_RFE_COUNT DNLLC(DN_RFE)
#define DNLL_TFE_COUNT DNLLC(DN_TFE)
#define DNLL_DRP_COUNT DNLLC(DN_DRP)

#define DNLLFP_NAME(b, idx, name) DNLL_PREFIX_NAME b "/" idx "/" name
#define DNLLFP_BASE(b, idx)  DNLLFP_NAME(b, idx, DNP_BASE)
#define DNLLFP_IRQ(b, idx)   DNLLFP_NAME(b, idx, DNP_IRQ)
#define DNLLFP_CORE(b, idx)  DNLLFP_NAME(b, idx, DNP_CORE)
#define DNLLFP_CFG_BASE(b, idx)  DNLLFP_NAME(b, idx, DNP_CFG_BASE)

#endif
