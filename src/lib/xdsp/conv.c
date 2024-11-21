// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv.h"
#include "conv_i16_f32_2.h"
#include "conv_ci16_2cf32_2.h"
#include "conv_i16_4f32_2.h"
#include "conv_f32_i16_2.h"
#include "conv_2cf32_ci16_2.h"
#include "conv_2ci16_ci16_2.h"
#include "conv_ci16_2ci16_2.h"
#include "conv_i12_f32_2.h"
#include "conv_f32_i12_2.h"
#include "conv_ci12_2cf32_2.h"
#include "conv_2cf32_ci12_2.h"

#include <strings.h>
#include <string.h>

static bool isI12(const char* s)
{
    return strcasecmp(s, "i12") == 0;
}

static bool isI16(const char* s)
{
    return strcasecmp(s, "i16") == 0;
}

static bool isF32(const char* s)
{
    return strcasecmp(s, "f32") == 0;
}

static bool isCI12(const char* s)
{
    return strcasecmp(s, "ci12") == 0;
}

static bool isCI16(const char* s)
{
    return strcasecmp(s, "ci16") == 0;
}

static bool isCF32(const char* s)
{
    return strcasecmp(s, "cf32") == 0;
}

static void tr_dummy(const void *__restrict *__restrict indata,
                     unsigned indatabsz,
                     void *__restrict *__restrict outdata,
                     unsigned outdatabsz)
{
    unsigned sz = indatabsz;
    if (sz > outdatabsz) sz = outdatabsz;

    memcpy(*outdata, *indata, sz);
}

static unsigned tr_dummy_sz(unsigned inbytes, bool reverse)
{
    return inbytes;
}

static unsigned tr_conv_i12_f32_sz(unsigned inbytes, bool reverse)
{
    if (reverse)
        return inbytes * 3 / 8;
    else
        return inbytes * 8 / 3;
}

static unsigned tr_conv_f32_i12_sz(unsigned inbytes, bool reverse)
{
    return tr_conv_i12_f32_sz(inbytes, !reverse);
}

static unsigned tr_conv_i16_f32_sz(unsigned inbytes, bool reverse)
{
    if (reverse)
        return inbytes >> 1;
    else
        return inbytes << 1;
}

static unsigned tr_conv_f32_i16_sz(unsigned inbytes, bool reverse)
{
    return tr_conv_i16_f32_sz(inbytes, !reverse);
}

static transform_info_t s_tr_none = { NULL, NULL };
static transform_info_t s_tr_dummy = { tr_dummy, tr_dummy_sz };

transform_info_t get_transform_fn(const char* from,
                                  const char* to,
                                  unsigned inveccnt,
                                  unsigned outveccnt)
{
    if((isCI16(to) && isCI12(from)) || (isCI16(from) && isCI12(to)))
    {
        return s_tr_none; //TODO!!!! implement transforms for ci16@ci12!
    }

    if (inveccnt == 1 && outveccnt == 2 && isCI16(from) && isCF32(to)) {
        transform_info_t l_conv_ci16_2f32 = { conv_get_ci16_2cf32(), tr_conv_i16_f32_sz };
        return l_conv_ci16_2f32;
    }
    
    if (inveccnt == 1 && outveccnt == 2 && isCI12(from) && isCF32(to)) {
        transform_info_t l_conv_ci12_2f32 = { conv_get_ci12_2cf32(), tr_conv_i12_f32_sz };
        return l_conv_ci12_2f32;
    }

    if (inveccnt == 1 && outveccnt == 2 && isCI16(from) && isCI16(to)) {
        transform_info_t l_conv_ci16_2ci16 = { conv_get_ci16_2ci16(), tr_dummy_sz };
        return l_conv_ci16_2ci16;
    }
    
    if (inveccnt == 1 && outveccnt == 4 && isI16(from) && isF32(to)) {
        transform_info_t l_conv_i16_4f32 = { conv_get_i16_4f32(), tr_conv_i16_f32_sz };
        return l_conv_i16_4f32;
    }

    if (inveccnt == 2 && outveccnt == 1 && isCF32(from) && isCI16(to)) {
        transform_info_t l_conv_2cf32_ci16 = { conv_get_2cf32_ci16(), tr_conv_f32_i16_sz };
        return l_conv_2cf32_ci16;
    }

    if (inveccnt == 2 && outveccnt == 1 && isCI16(from) && isCI16(to)) {
        transform_info_t l_conv_2ci16_ci16 = { conv_get_2ci16_ci16(), tr_dummy_sz };
        return l_conv_2ci16_ci16;
    }
    
    if (inveccnt == 2 && outveccnt == 1 && isCF32(from) && isCI12(to)) {
        transform_info_t l_conv_ci12_2f32 = { conv_get_2cf32_ci12(), tr_conv_f32_i12_sz };
        return l_conv_ci12_2f32;
    }

    if (inveccnt != 1 || outveccnt != 1)
        return s_tr_none;

    if ((isI16(from) && isF32(to)) ||
            (isCI16(from) && isCF32(to))) {
        transform_info_t l_conv_i16_f32 = { conv_get_i16_f32(), tr_conv_i16_f32_sz };
        return l_conv_i16_f32;
    }

    if ((isF32(from) && isI16(to)) ||
            (isCF32(from) && isCI16(to))) {
        transform_info_t l_conv_f32_i16 = { conv_get_f32_i16(), tr_conv_f32_i16_sz };
        return l_conv_f32_i16;
    }

    if ((isI12(from) && isF32(to)) ||
            (isCI12(from) && isCF32(to))) {
        transform_info_t l_conv_i12_f32 = { conv_get_i12_f32(), tr_conv_i12_f32_sz };
        return l_conv_i12_f32;
    }

    if ((isF32(from) && isI12(to)) ||
        (isCF32(from) && isCI12(to))) {
        transform_info_t l_conv_f32_i12 = { conv_get_f32_i12(), tr_conv_f32_i12_sz };
        return l_conv_f32_i12;
    }

    return s_tr_dummy;
}

bool is_transform_dummy(conv_function_t t)
{
    return t == tr_dummy;
}
