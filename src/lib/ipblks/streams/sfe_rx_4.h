// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SFE_RX_4
#define SFE_RX_4

#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>

#include "streams.h"

struct sfe_cfg {
    lldev_t dev;
    subdev_t subdev;

    unsigned cfg_fecore_id;

    unsigned cfg_base;
    unsigned cfg_fifomaxbytes;
    unsigned cfg_word_bytes;
    unsigned cfg_raw_chans;
};
typedef struct sfe_cfg sfe_cfg_t;


int sfe_rx4_check_format(const struct stream_config* psc);

int sfe_rx4_configure(const sfe_cfg_t *fe,
                      const struct stream_config* psc,
                      struct fifo_config* pfc);

int sfe_rx4_throttle(const sfe_cfg_t* fe, bool enable, uint8_t send, uint8_t skip);

int sfe_rx4_startstop(const sfe_cfg_t *fe, bool start);

int sfe_rf4_nco_enable(const sfe_cfg_t *fe, bool enable, unsigned iqaccum);

int sfe_rf4_nco_freq(const sfe_cfg_t* fe, int32_t freq);


int exfe_rx4_configure(const sfe_cfg_t* fe,
                       const struct stream_config* psc,
                       struct fifo_config* pfc);

int exfe_rx4_update_chmap(const sfe_cfg_t* fe,
                          bool complex,
                          unsigned total_chan_num,
                          const channel_info_t *newmap);



#endif
