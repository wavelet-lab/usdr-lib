// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USDR_HELPER_H
#define USDR_HELPER_H

#include <usdr_lowlevel.h>

/**
 * @brief usdr_read_file Read the whole file into memory
 * @param filename File to read
 * @param len Number of bytes actually read
 * @return Pointer to allocated memory, need to be freed
 */
void* usdr_read_file(const char* filename,
                     size_t* len);



#endif
