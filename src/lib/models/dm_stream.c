// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dm_stream.h"
#include "dm_dev_impl.h"

#include "../ipblks/streams/streams_api.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TX_TO_FILE_IMITATION

#ifdef TX_TO_FILE_IMITATION

#include <stdio.h>
static char* tx_imit_fname = "./tx_imit.iq";
FILE* tx_imit_file = NULL;
static usdr_dms_nfo_t snfo_tx;

#endif

int usdr_dms_destroy(pusdr_dms_t stream)
{
#ifdef TX_TO_FILE_IMITATION
    if(tx_imit_file)
    {
        fclose(tx_imit_file);
        tx_imit_file = NULL;
    }
#endif

    struct stream_handle* h = (struct stream_handle*)stream;
    return h->dev->unregister_stream(h->dev, h);
}

int usdr_dms_info(pusdr_dms_t stream, usdr_dms_nfo_t* nfo)
{
    struct stream_handle* h = (struct stream_handle*)stream;
    return h->ops->stat(h, nfo);
}


int usdr_dms_create(pdm_dev_t device,
                    const char* sobj,
                    const char* dformat,
                    logical_ch_msk_t channels,
                    unsigned pktsyms,
                    pusdr_dms_t* outu)
{
    return usdr_dms_create_ex(device,
                              sobj,
                              dformat,
                              channels,
                              pktsyms,
                              0,
                              outu);
}

int usdr_dms_create_ex(pdm_dev_t device,
                       const char* sobj,
                       const char* dformat,
                       logical_ch_msk_t channels,
                       unsigned pktsyms,
                       unsigned flags,
                       pusdr_dms_t* outu)
{
    // Only up to 64 channels supported in old API
    usdr_channel_info_t chans;
    unsigned chan_map[64];

    unsigned cnt;
    unsigned i;
    for (cnt = 0, i = 0; i < 64; i++) {
        if (channels & (((uint64_t)1) << i)) {
            chan_map[cnt++] = i;
        }
    }

    if (cnt == 0) {
        return -EINVAL;
    }

    chans.count = cnt;
    chans.flags = 0;
    chans.phys_names = NULL;
    chans.phys_nums = chan_map;

    return usdr_dms_create_ex2(device, sobj, dformat,
                               &chans, pktsyms, flags, NULL, outu);
}

int usdr_dms_create_ex2(pdm_dev_t device,
                        const char* sobj,
                        const char* dformat,
                        const usdr_channel_info_t *channels,
                        unsigned pktsyms,
                        unsigned flags,
                        const char* parameters,
                        pusdr_dms_t* outu)
{
#ifdef TX_TO_FILE_IMITATION
    if(!tx_imit_file)
    {
        tx_imit_file = fopen(tx_imit_fname, "wb+c");
    }
#endif

    device_t* dev = device->lldev->pdev;
    int res = dev->create_stream(dev, sobj, dformat,
                                 channels, pktsyms, flags, parameters,
                                 (stream_handle_t**)outu);
    return res;
}

int usdr_dms_get_fd(pusdr_dms_t stream)
{
    struct stream_handle* h = (struct stream_handle*)stream;
    int64_t fd;

    int res = h->ops->option_get(h, "fd", &fd);
    if (res)
        return res;

    return fd;
}

int usdr_dms_set_ready(pusdr_dms_t stream)
{
    struct stream_handle* h = (struct stream_handle*)stream;
    return h->ops->option_set(h, "ready", 1);
}

int usdr_dms_op(pusdr_dms_t stream,
                unsigned command,
                dm_time_t tm)
{
    struct stream_handle* h = (struct stream_handle*)stream;
    return h->ops->op(h, command, tm);
}

int usdr_dms_sync(pdm_dev_t device,
                  const char* synctype, unsigned scount, pusdr_dms_t *pstream)
{
    device_t* dev = device->lldev->pdev;
    return dev->timer_op(dev, (stream_handle_t** )pstream, scount, synctype);
}


int usdr_dms_recv(pusdr_dms_t stream,
                  void **stream_buffs,
                  unsigned timeout_ms,
                  usdr_dms_recv_nfo_t* nfo)
{
    struct stream_handle* h = (struct stream_handle*)stream;
    return h->ops->recv(h, (char**)stream_buffs, timeout_ms, nfo);
}

#ifdef TX_TO_FILE_IMITATION
static int tx_dumper(pusdr_dms_t stream, const void **stream_buffs, unsigned samples)
{
    if(!tx_imit_file || usdr_dms_info(stream, &snfo_tx))
        return -EIO;

    unsigned bzsize = ((snfo_tx.pktbszie << 3) / snfo_tx.pktsyms * samples) >> 3;
    int res = fwrite(stream_buffs[0], bzsize , 1, tx_imit_file);
    return (res == 1) ? 0 : -EIO;
}
#endif

int usdr_dms_send(pusdr_dms_t stream,
                  const void **stream_buffs,
                  unsigned samples,
                  dm_time_t timestamp,
                  unsigned timeout_ms)
{
    struct stream_handle* h = (struct stream_handle*)stream;
    int res =  h->ops->send(h, (const char**)stream_buffs, samples, timestamp, timeout_ms, NULL);
#ifdef TX_TO_FILE_IMITATION
    res = res ? res : tx_dumper(stream, stream_buffs, samples);
#endif
    return res;
}

int usdr_dms_send_stat(pusdr_dms_t stream,
                       const void **stream_buffs,
                       unsigned samples,
                       dm_time_t timestamp,
                       unsigned timeout_ms,
                       usdr_dms_send_stat_t* stat)
{
    struct stream_handle* h = (struct stream_handle*)stream;
    int res = h->ops->send(h, (const char**)stream_buffs, samples, timestamp, timeout_ms, stat);
#ifdef TX_TO_FILE_IMITATION
    res = res ? res : tx_dumper(stream, stream_buffs, samples);
#endif
    return res;
}
