// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "parse_params.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

void parse_params(const char* params, char delimeter, const char** plist, struct param_data* out, const char **unrecognezed)
{
    const char* p = params;
    const char* d;

    if (params == NULL)
        return;

    while (*p == delimeter)
        p++;

    do {
        unsigned j = 0;
        const char* pl;
        size_t plen;

        d = strchr(p, delimeter);
        if (d) {
            plen = d - p;
        } else {
            plen = strlen(p);
        }

        while ((pl = plist[j++])) {
            size_t w = strlen(pl);
            if (plen < w)
                continue;
            if (w == 0 || strncmp(pl, p, w) == 0) {
                out[j - 1].item = p + w;
                out[j - 1].item_len = plen - w;
                goto found;
            }
        }

        if (unrecognezed) {
            *unrecognezed = p;
        }

found:
        p = d + 1;
    } while (d != NULL);
}

int is_param_on(struct param_data* p)
{
    if (p->item_len == 2 && strncmp(p->item, "on", 2) == 0) {
        return 1;
    }
    if (p->item_len == 2 && strncmp(p->item, "en", 2) == 0) {
        return 1;
    }
    if (p->item_len == 3 && strncmp(p->item, "off", 3) == 0) {
        return 0;
    }
    if (p->item_len == 1) {
        if (*p->item == '1')
            return 1;
        if (*p->item == '0')
            return 0;
    }
    return -1;
}


int get_param_long(struct param_data* p, long* val)
{
    char *endptr;
    long int res;
    if (p->item == NULL)
        return -EINVAL;

    res = strtol(p->item, &endptr, 10);
    if (endptr) {
        if (endptr < p->item + p->item_len)
            return -EINVAL;
    }
    *val = res;
    if (res == LONG_MIN || res == LONG_MAX) {
        return -errno;
    }
    return 0;
}
