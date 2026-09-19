// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "usdr_helpers.h"
#include "usdr_logging.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


void* usdr_read_file(const char* filename, size_t* len)
{
    char *buffer = NULL;
    off_long_t length;
    FILE * f = fopen(filename, "rb");
    if (!f) {
        int err = errno;
        USDR_LOG("FILE", USDR_LOG_CRITICAL_WARNING,
                 "Unable to open file %s: error %s (%d)\n",
                 filename, strerror(err), err);

        return NULL;
    }

    fseek(f, 0, SEEK_END);
    length = ftell_long(f);
    fseek(f, 0, SEEK_SET);

    if (length > INTPTR_MAX) {
        fclose(f);
        USDR_LOG("FILE", USDR_LOG_CRITICAL_WARNING, "File is too big!\n");
        return NULL;
    }

    buffer = (char*)malloc(length);
    if (buffer) {
        size_t rd = fread(buffer, 1, length, f);

        USDR_LOG("FILE", USDR_LOG_DEBUG,
                 "File %s was read %llu bytes\n",
                 filename, (long long)rd);
        *len = rd;
    }
    fclose(f);
    return buffer;
}
