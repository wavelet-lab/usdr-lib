// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DM_STREAM_H
#define DM_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/** @file dm_stream.h User friendly stream function */
#include <usdr_port.h>
#include "dm_time.h"

struct usdr_dms;
typedef struct usdr_dms usdr_dms_t;
typedef usdr_dms_t* pusdr_dms_t;

enum usdr_dms_dir {
    USDR_DMS_TX,
    USDR_DMS_RX,
};

struct usdr_dms_nfo {
    unsigned type;
    unsigned channels;
    unsigned pktbszie; // packet size in bytes for each channel
    unsigned pktsyms;  // packet size in symbols for each channel
    unsigned totsamptick; // number of all samples in all channels for one time tick
    unsigned burst_count; // bursts in packet
};
typedef struct usdr_dms_nfo usdr_dms_nfo_t;

struct usdr_dms_stat {
    uint64_t wire_bytes;
    uint64_t host_bytes;
    uint64_t total_symbols;
    uint64_t symbols_lost;  // Underrun overrun
    uint64_t spurios_op;
};

// Bit mask of logical channels in use in the stream
typedef uint64_t logical_ch_msk_t;

// HOST_FORMAT@WIRE_FORMAT
// string stream format  cf32@ci16;2;default

int usdr_dms_create(pdm_dev_t device,
                    const char* sobj,
                    const char* dformat,
                    logical_ch_msk_t channels,
                    unsigned pktsyms,
                    pusdr_dms_t* outu);

enum {
    DMS_FLAG_NEED_FD = 1,
    DMS_FLAG_NEED_TX_STAT = 2,
};
int usdr_dms_create_ex(pdm_dev_t device,
                       const char* sobj,
                       const char* dformat,
                       logical_ch_msk_t channels,
                       unsigned pktsyms,
                       unsigned flags,
                       pusdr_dms_t* outu);

struct usdr_dms_frame_nfo {
    dm_time_t time;
    unsigned samples;
};

struct usdr_dms_recv_nfo {
    dm_time_t fsymtime;
    unsigned totsyms; // Number of valid samples in the buffers
    unsigned totlost; // Number of lost samples in the frame
    unsigned max_parts;
    uint64_t extra;
    struct usdr_dms_frame_nfo parts[0];
};

int usdr_dms_recv(pusdr_dms_t stream,
                  void **stream_buffs,
                  unsigned timeout,
                  struct usdr_dms_recv_nfo* nfo);

int usdr_dms_send(pusdr_dms_t stream,
                  const void **stream_buffs,
                  unsigned samples,
                  dm_time_t timestamp,
                  unsigned timeout);

int usdr_dms_destroy(pusdr_dms_t stream);

int usdr_dms_info(pusdr_dms_t stream, usdr_dms_nfo_t* nfo);

// get fd for poll() like operation
int usdr_dms_get_fd(pusdr_dms_t stream);

int usdr_dms_set_ready(pusdr_dms_t stream);

// none   - no syncing beetween streams
// all    - sync between all active streams
// extall - sync between all active streams on extrenal sync event (onepps)
//
// this function should be called after all calls of usdr_dms_create() but before usdr_dms_op()
int usdr_dms_sync(pdm_dev_t device,
                  const char* synctype,
                  unsigned scount,
                  pusdr_dms_t *pstream);

enum usdr_dms_op_types {
    USDR_DMS_START,
    USDR_DMS_STOP,
    USDR_DMS_START_AT,
    USDR_DMS_STOP_AT,
};

/// Stream operation function
int usdr_dms_op(pusdr_dms_t stream,
                unsigned command,
                dm_time_t tm);


#ifdef __cplusplus
}
#endif

#endif
