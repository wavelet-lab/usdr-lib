// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef ATTRIBUTE_SWITCH_H
#define ATTRIBUTE_SWITCH_H

#ifdef __EMSCRIPTEN__
#define VWLT_ATTRIBUTE(...)
#else
#define VWLT_ATTRIBUTE(...) __attribute__((__VA_ARGS__))
#endif

#endif // ATTRIBUTE_SWITCH_H
