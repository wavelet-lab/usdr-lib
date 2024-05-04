// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LIMESDR_CTRL_H
#define LIMESDR_CTRL_H

#include <stdint.h>
#include <usdr_port.h>
#include <usdr_lowlevel.h>

#include "../m2_lm7_1/lms7002m_ctrl.h"

struct limesdr_dev
{
    lms7002_dev_t base;
};
typedef struct limesdr_dev limesdr_dev_t;



int limesdr_ctor(lldev_t dev, limesdr_dev_t *d);
int limesdr_init(limesdr_dev_t *d);
int limesdr_dtor(limesdr_dev_t *d);

int limesdr_set_samplerate(limesdr_dev_t *d, unsigned rx_rate, unsigned tx_rate,
                           unsigned adc_rate, unsigned dac_rate);


int limesdr_prepare_streaming(limesdr_dev_t *d);
int limesdr_setup_stream(limesdr_dev_t *d, bool iq12bit, bool sisosdr, bool trxpulse);
int limesdr_disable_stream(limesdr_dev_t *d);


int limesdr_reset_timestamp(limesdr_dev_t *d);
int limesdr_stop_streaming(limesdr_dev_t *d);
int limesdr_start_streaming(limesdr_dev_t *d);


struct limesdr_data_hdr
{
    uint8_t reserved[8];
    uint64_t counter;
};
typedef struct limesdr_data_hdr limesdr_data_hdr_t;


#endif
