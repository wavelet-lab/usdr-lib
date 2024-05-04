// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "streams.h"
#include <string.h>

struct bitsfmt get_bits_fmt(const char* fmt)
{
    struct bitsfmt bmft = { 0, false, {0, } };
    if (*fmt == 'c' || *fmt == 'C') {
        fmt++;
        bmft.complex = true;
    }

    if (strcasecmp(fmt, "i8") == 0)
        bmft.bits = 8;
    else if (strcasecmp(fmt, "i12") == 0)
        bmft.bits = 12;
    else if (strcasecmp(fmt, "i16") == 0)
        bmft.bits = 16;
    else
        strncpy((char*)bmft.func, fmt, sizeof(bmft.func));

    return bmft;
}

int stream_parse_dformat(char* dmft, struct parsed_data_format* ofmt)
{
    char* saveptr;

    ofmt->host_fmt = strtok_r(dmft, "@", &saveptr);
    ofmt->wire_fmt = strtok_r(NULL, "@", &saveptr);

    return 0;
}
