// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DM_SDR_H
#define DM_SDR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dm_dev.h"


typedef uint64_t usdr_frequency_t; // in Hz/1e6

// Set data channel bandwith like external SAW, IF SAW, zero IF filer, etc.
int usdr_dmsdr_set_bandwidth(pdm_dev_t dev,
                             const char *entity,
                             usdr_frequency_t start,
                             usdr_frequency_t stop);

// Set LO frequencies like RFIC mixer, external LOs, DDC/DUC
int usdr_dmsdr_set_frequency(pdm_dev_t dev,
                             const char *entity,
                             usdr_frequency_t freq);

// Set gain stages like LNA, PGA, VGA, etc
int usdr_dmsdr_set_gain(pdm_dev_t dev,
                        const char *entity,
                        unsigned gain);


int usdr_dmsdr_set_path(pdm_dev_t dev,
                        const char *entity,
                        unsigned path);

int usdr_dmsdr_set_path_str(pdm_dev_t dev,
                        const char *entity,
                        const char *p);

#ifdef __cplusplus
}
#endif

#endif
