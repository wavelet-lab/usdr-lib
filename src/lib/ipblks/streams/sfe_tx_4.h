// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SFE_TX_4
#define SFE_TX_4

#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>

#include "streams.h"

int sfe_tx4_check_format(const struct stream_config* psc);

// TX only supports 16 bit for now
int sfe_tx4_ctl(lldev_t dev,
                subdev_t subdev,
                unsigned cfg_base,
                bool mimo,
                bool repeat,
                bool start);


int sfe_tx4_push_ring_buffer(lldev_t dev,
                             subdev_t subdev,
                             unsigned cfg_base,
                             unsigned samples,
                             int64_t timestamp);

#endif
