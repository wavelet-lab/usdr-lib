// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "base64.h"

static
unsigned int b64_int(unsigned int ch) {

    // ASCII to base64
    // 65-90  Upper Case  >>  0-25
    // 97-122 Lower Case  >>  26-51
    // 48-57  Numbers     >>  52-61
    // 43     Plus (+)    >>  62
    // 47     Slash (/)   >>  63
    // 61     Equal (=)   >>  64~

    if (ch == 43)
        return 62;

    if (ch == 47)
        return 63;

    if (ch == 61)
        return 64;

    if ((ch > 47) && (ch < 58))
        return ch + 4;

    if ((ch > 64) && (ch < 91))
        return ch - 'A';

    if ((ch > 96) && (ch < 123))
        return (ch - 'a') + 26;

    return 0;
}

static const
char b64_chr[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


int base64_encode(const char* src, unsigned bsz, char* out)
{
    unsigned int i = 0, k = 0, s[3];

    while (i < bsz - 2) {
        s[0] = ((unsigned char*)src)[i++];
        s[1] = ((unsigned char*)src)[i++];
        s[2] = ((unsigned char*)src)[i++];

        out[k + 0] = b64_chr[ (s[0] & 0xFF) >> 2 ];
        out[k + 1] = b64_chr[ ((s[0] & 0x03) << 4) + ((s[1] & 0xF0) >> 4) ];
        out[k + 2] = b64_chr[ ((s[1] & 0x0F) << 2) + ((s[2] & 0xC0) >> 6) ];
        out[k + 3] = b64_chr[ s[2] & 0x3F ];

        k += 4;
    }

    unsigned j = 0;
    for (; i < bsz; ) {
        s[j++] = ((unsigned char*)src)[i++];
    }

    if (j) {
        if (j == 1) {
            s[1] = 0;
        }

        out[k + 0] = b64_chr[ (s[0] & 0xFF) >> 2 ];
        out[k + 1] = b64_chr[ ((s[0] & 0x03) << 4) + ((s[1] & 0xF0) >> 4) ];

        if (j == 2) {
            out[k + 2] = b64_chr[ ((s[1] & 0x0F) << 2) ];
        } else {
            out[k + 2] = '=';
        }

        out[k + 3] = '=';
        k += 4;
    }

    out[k] = 0;
    return k;
}

int base64_decode(const char* src, unsigned bsz, char* out)
{
    unsigned int i = 0, j = 0, k = 0, s[4];

    for (i = 0; i < bsz; i++) {
        s[j++] = b64_int(src[i]);

        if (j == 4) {
            out[k + 0] = ((s[0] & 0xFF) << 2) + ((s[1] & 0x30) >> 4);

            if (s[2] != 64) {
                out[k + 1] = ((s[1] & 0x0F) << 4) + ((s[2] & 0x3C) >> 2);

                if ((s[3] != 64)) {
                    out[k + 2] = ((s[2] & 0x03) << 6) + (s[3]);
                    k += 3;
                } else {
                    k += 2;
                }
            } else {
                k += 1;
            }
            j = 0;
        }
    }

    return k;
}
