// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef PARSE_PARAMS_H
#define PARSE_PARAMS_H

#include <stddef.h>

struct param_data {
    const char* item;
    size_t item_len;
};

void parse_params(const char* params, char delimeter, const char** plist, struct param_data* out,
                  const char** unrecognezed);


// Returns 0 - false; 1 - true; -1 - not recognised
int is_param_on(struct param_data* p);

int get_param_long(struct param_data* p, long* val);

#endif
