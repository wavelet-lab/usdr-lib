// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef BASE64_H
#define BASE64_H

int base64_encode(const char* src, unsigned bsz, char* out);
int base64_decode(const char* src, unsigned bsz, char* out);

#endif
