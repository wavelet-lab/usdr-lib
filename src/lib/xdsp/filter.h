// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>

struct filter_data;
typedef struct filter_data filter_data_t;


// Real taps only

filter_data_t* filter_data_alloc(unsigned origblksz,
                                const int16_t* pfilter,
                                unsigned filer_taps,
                                unsigned decim_inter,
                                unsigned flags);
void filter_data_free(filter_data_t* o);

/* get pointer to store input data */
int16_t* filter_data_ptr(filter_data_t* o);
int16_t* filter_data_ptr2(filter_data_t* o);

/* process filtration using stored data and save history for next step */
void filter_data_process(filter_data_t* o, int16_t* out);

/* processing block size in samples, for complex the total number of real and complex vals */
unsigned filter_block_size(filter_data_t* o);
#endif
