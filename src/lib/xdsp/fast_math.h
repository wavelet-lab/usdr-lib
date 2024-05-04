// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef FAST_MATH_H
#define FAST_MATH_H

#include <stdint.h>

#define WVLT_FASTLOG2_MUL    1.1920928955078125E-7f
#define WVLT_FASTLOG2_SUB    126.94269504f

/*
 * Log2 Mitchell’s Approximation
 */
static inline
float wvlt_fastlog2(float x)
{
    union {float f32; uint32_t u32;} u_fi = { x };
    float y = u_fi.u32;
    y *= WVLT_FASTLOG2_MUL;
    y -= WVLT_FASTLOG2_SUB;
    return y;
}

#endif // FAST_MATH_H
