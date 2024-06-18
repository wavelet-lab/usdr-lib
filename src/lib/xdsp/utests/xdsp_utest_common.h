// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef XDSP_UTEST_COMMON_H
#define XDSP_UTEST_COMMON_H

#include <time.h>
#include <inttypes.h>
#include <math.h>

#define ALIGN_BYTES (size_t)64

static inline uint64_t clock_get_time()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000LL + (uint64_t)ts.tv_nsec/1000LL;
}

#endif // XDSP_UTEST_COMMON_H
