// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SFE_RX_4
#define SFE_RX_4

#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>

#include "streams.h"

int sfe_rx4_check_format(const struct stream_config* psc);

int sfe_rx4_configure(lldev_t dev,
                      subdev_t subdev,
                      unsigned cfg_base,
                      unsigned cfg_fifomaxbytes,
                      const struct stream_config* psc,
                      struct fifo_config* pfc);

int sfe_rx4_throttle(lldev_t dev,
                     subdev_t subdev,
                     unsigned cfg_base,
                     bool enable,
                     uint8_t send,
                     uint8_t skip);

int sfe_rx4_startstop(lldev_t dev,
                      subdev_t subdev,
                      unsigned cfg_base,
                      stream_time_t time,
                      bool start);

int sfe_rf4_nco_enable(lldev_t dev,
                       subdev_t subdev,
                       unsigned cfg_base,
                       bool enable,
                       unsigned iqaccum);

int sfe_rf4_nco_freq(lldev_t dev,
                     subdev_t subdev,
                     unsigned cfg_base,
                     int32_t freq);



#endif
