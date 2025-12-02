// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SFE_RX_4
#define SFE_RX_4

#include "sfe_txrx_4.h"

int sfe_rx4_check_format(const struct stream_config* psc);

int sfe_rx4_configure(const sfe_cfg_t *fe,
                      const struct stream_config* psc,
                      struct fifo_config* pfc, uint64_t *pwr_ch_mask);

int sfe_rx4_throttle(const sfe_cfg_t* fe, bool enable, uint8_t send, uint8_t skip);

int sfe_rx4_startstop(const sfe_cfg_t *fe, bool start);

int sfe_rf4_nco_enable(const sfe_cfg_t *fe, bool enable, unsigned iqaccum);

int sfe_rf4_nco_freq(const sfe_cfg_t* fe, int32_t freq);


int exfe_rx4_configure(const sfe_cfg_t* fe,
                       const struct stream_config* psc,
                       struct fifo_config* pfc);

int exfe_trx4_update_chmap(const sfe_cfg_t* fe,
                           bool mask,
                           bool complex,
                           unsigned total_chan_num,
                           const channel_info_t *newmap);

int exfe_tx4_mute(const sfe_cfg_t* fe, uint64_t mutemask);

int exfe_tx4_config(const sfe_cfg_t* fe, unsigned bmt, unsigned expand);

#endif
