// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SFE_TX_4
#define SFE_TX_4

#include "sfe_txrx_4.h"

int sfe_tx4_check_format(const struct stream_config* psc);
int sfe_extx4_check_format(const struct stream_config* psc, const sfe_cfg_t *fecfg);

int sfe_tx4_mtu_get(const struct stream_config* psc);

// TX only supports 16 bit for now
int sfe_tx4_ctl(sfe_cfg_t *pfe,
                unsigned cfg_base,
                unsigned chans,
                uint8_t swap_ab_flag,
                uint8_t mute_flag,
                bool repeat,
                bool start);

int sfe_tx4_upd(sfe_cfg_t *pfe,
                unsigned cfg_base,
                unsigned mute_flags,
                unsigned swap_ab_flag);

int fe_tx4_swap_ab_get(unsigned channels,
                       const channel_info_t* newmap,
                       unsigned* swap_ab);


int sfe_tx4_push_ring_buffer(lldev_t dev,
                             subdev_t subdev,
                             unsigned cfg_base,
                             unsigned samples,
                             int64_t timestamp);

#endif
