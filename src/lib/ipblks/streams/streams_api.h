// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef STREAMS_API_H
#define STREAMS_API_H

#include <stdint.h>
#include "streams.h"

#include "../../device/device.h"
#include "../../models/dm_stream.h"

struct stream_ops {
    int (*destroy)(stream_handle_t* stream_handle);

    // Operations
    int (*op)(stream_handle_t* stream, unsigned command, dm_time_t timestamp);

    int (*recv)(stream_handle_t* stream,
                char **stream_buffs,
                unsigned timeout_ms,
                struct usdr_dms_recv_nfo* nfo);

    int (*send)(stream_handle_t* stream,
                const char **stream_buffs,
                unsigned samples,
                dm_time_t timestamp,
                unsigned timeout_ms,
                usdr_dms_send_stat_t* stat);

    int (*stat)(stream_handle_t*, usdr_dms_nfo_t* nfo);

    // Custom stream options
    int (*option_get)(stream_handle_t*, const char* name, int64_t* out_val);
    int (*option_set)(stream_handle_t*, const char* name, int64_t in_val);
};
typedef struct stream_ops stream_ops_t;

// Basic stream handle
struct stream_handle {
    device_t *dev;
    const stream_ops_t* ops;
};
typedef struct stream_handle stream_handle_t;


#endif
