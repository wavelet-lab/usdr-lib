// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <inttypes.h>

#include "stream_limesdr.h"

#include <usdr_logging.h>

#include "../../xdsp/conv.h"

//
// TODO: OP calls don't work at the moment
// TODO: Dynamic RX packet reconfiguration doesn't work
//


struct stream_partial_rx {

    // Buffer with outstanding data
    char* dma_buf;
    //uint64_t last_burst_time;

    unsigned wire_cons_smpl;  // Samples consumed from outstanding buffer
    unsigned wire_cons_bytes; // Bytes consumed
    unsigned wire_bidx;
};

struct stream_limesdr {
    struct stream_handle base;

    unsigned host_pkt_symbs; //Host symbols per req packet

    unsigned bps;            // bits per symbol on wire

    conv_function_t tf_data;
    size_function_t tf_size;

    stream_t ll_streamo;

    // Streaming parameters
    unsigned burst_count; // Bursts in packet
    unsigned burst_symbs;
    unsigned burst_bytes;     // Busrt bytes contating samples
    unsigned burst_host_bytes;
    unsigned block_samples; // Number samples in one process block (4K)
    unsigned tx_sampl_c;
    int fd;

    // Stats
    uint64_t blk_time_prev;
    uint64_t lag_remaining; //Number of ZERO samples need to instert into host buffer to prevent misalign

    struct stream_partial_rx outst;
    uint64_t rcnt;
    uint64_t overruns;

};
typedef struct stream_limesdr stream_limesdr_t;

static
int _limestr_destroy(stream_handle_t* str)
{
    int res;
    stream_limesdr_t* stream = (stream_limesdr_t*)str;
    lldev_t dev = stream->base.dev->dev;
    //struct lowlevel_ops* ops = lowlevel_get_ops(dev);

    USDR_LOG("DSTR", USDR_LOG_DEBUG, "Destroying strem %d\n", stream->ll_streamo);

    res = dev->pdev->unregister_stream(dev->pdev, str);
    free(stream);
    return res;
}

#define TIMESTAMP_NONE ~0ULL
#define LIMESDR_STREAM_HDR  16

static
int _limestr_stream_recv(stream_handle_t* str,
                         char** stream_buffs,
                         unsigned timeout,
                         struct usdr_dms_recv_nfo* nfo)
{
    int res;
    stream_limesdr_t* stream = (stream_limesdr_t*)str;
    lldev_t dev = stream->base.dev->dev;
    struct lowlevel_ops* ops = lowlevel_get_ops(dev);

    char* dma_buf;
    unsigned host_bps = (stream->burst_host_bytes / stream->burst_symbs);
    unsigned wire_bps = (stream->burst_bytes / stream->burst_symbs);
    unsigned host_buf_off = 0;
    unsigned wire_buf_off;
    unsigned host_smpl_rem = stream->host_pkt_symbs;
    unsigned wire_smpl_consumed;
    unsigned wire_pkt_smpl = stream->burst_count * stream->burst_symbs;
    unsigned bidx; //Burst index in the lowlevel buffer
    //unsigned

    uint64_t brst_time;
    uint64_t fsym_time = ~0ULL;

    do {
        if (stream->outst.dma_buf) {
            dma_buf = stream->outst.dma_buf;
            wire_smpl_consumed = stream->outst.wire_cons_smpl;
            brst_time = stream->blk_time_prev; //stream->outst.last_burst_time;
            wire_buf_off = stream->outst.wire_cons_bytes;
            bidx = stream->outst.wire_bidx;
        } else {
            // TODO: obtain packet size for dynamic packet rescaling

            res = ops->recv_dma_wait(dev, 0,
                                     stream->ll_streamo,
                                     (void**)&dma_buf, NULL, NULL, timeout);
            if (res < 0)
                return res;

            wire_smpl_consumed = 0;
            wire_buf_off = 0;
            bidx = ~0U;
            stream->rcnt++;

#if 0
            // Manual loss insertion to emulate packet overruns
            res = rand();
            if (res < RAND_MAX / 10) {
                res = ops->recv_dma_release(dev, 0,
                                            stream->ll_streamo, dma_buf);
                continue;
            }
#endif
        }

        do {
            unsigned burst_idx_off = (wire_buf_off % (stream->burst_bytes + LIMESDR_STREAM_HDR));
            unsigned burst_symbs = stream->burst_symbs;

            if (burst_idx_off == 0) {
                brst_time = *(uint64_t*)(dma_buf + 8 + wire_buf_off);

                uint64_t brstlag = (stream->blk_time_prev == ~0ul) ? 0 : (brst_time - stream->block_samples - stream->blk_time_prev);
                if (brstlag != 0) {
                    USDR_LOG("DSTR", USDR_LOG_ERROR, "RECV [%8" PRId64 "] BURST LAG %" PRIu64 " @ %" PRId64 "  OVERRUNS: %" PRId64 " =================================================\n",
                             (int64_t)stream->rcnt,
                             brstlag, (int64_t)brst_time,
                             (int64_t)stream->overruns);
                }
                stream->blk_time_prev = brst_time;
                stream->lag_remaining = brstlag;

                wire_buf_off += LIMESDR_STREAM_HDR;
                bidx++;
            } else {
                burst_symbs -= wire_smpl_consumed % stream->burst_symbs;
            }

            uint64_t lag_rem = stream->lag_remaining;

            if (stream->lag_remaining != 0) {
                if (host_buf_off == 0) {
                    stream->overruns += (stream->lag_remaining / stream->host_pkt_symbs);
                    stream->lag_remaining = (stream->lag_remaining % (stream->host_pkt_symbs));

                    fsym_time = brst_time - stream->lag_remaining;

                    // Fill with zeroes
                    memset(stream_buffs[0] + host_buf_off, 0, stream->lag_remaining * host_bps);

                    host_buf_off += stream->lag_remaining * host_bps;
                    host_smpl_rem -= stream->lag_remaining;

                    stream->lag_remaining = 0;
                } else {
                    unsigned samples_needed = (stream->lag_remaining > host_smpl_rem) ? host_smpl_rem : stream->lag_remaining;

                    memset(stream_buffs[0] + host_buf_off, 0, samples_needed * host_bps);

                    host_buf_off += samples_needed * host_bps;
                    host_smpl_rem -= samples_needed;

                    stream->lag_remaining -= samples_needed;
                }
            }

            unsigned wire_smpl_rem = stream->burst_count * stream->burst_symbs - wire_smpl_consumed;
            if (wire_smpl_rem > burst_symbs)
                wire_smpl_rem = burst_symbs;
            unsigned samples_needed = (host_smpl_rem > wire_smpl_rem) ? wire_smpl_rem : host_smpl_rem;
            unsigned wire_proc_bytes = samples_needed * wire_bps;
            unsigned host_proc_bytes = samples_needed * host_bps;

            if (samples_needed == 0)
                break;

            if (host_buf_off == 0) {
                fsym_time = brst_time + wire_smpl_consumed % stream->block_samples;
            }

            USDR_LOG("DSTR", USDR_LOG_NOTE, "RECV [%8lld/%d] TIME %016lld / %016lld -- HOST_OFF %6d WIRE_OFF %6d WIRE_CONSUMED %6d -- TAKE %d -- LAG REM %lld\n",
                     (long long)stream->rcnt, bidx,
                     (long long)brst_time, (long long)fsym_time,
                     host_buf_off, wire_buf_off, wire_smpl_consumed, samples_needed,
                     (long long)lag_rem);

            const void* dma_bufs_bias[] = {
                dma_buf + wire_buf_off,
            };
            void* strem_bufs_bias[] = {
                stream_buffs[0] + host_buf_off,
            };

            stream->tf_data(dma_bufs_bias, wire_proc_bytes, strem_bufs_bias, host_proc_bytes);

            wire_buf_off += wire_proc_bytes;
            host_buf_off += host_proc_bytes;
            host_smpl_rem -= samples_needed;
            wire_smpl_consumed += samples_needed;

        } while (host_smpl_rem != 0 && wire_smpl_consumed != wire_pkt_smpl);

        if (wire_smpl_consumed == wire_pkt_smpl) {
            stream->outst.dma_buf = NULL;

            res = ops->recv_dma_release(dev, 0,
                                        stream->ll_streamo, dma_buf);
            if (res)
                return res;
        } else {
            stream->outst.dma_buf = dma_buf;
            //stream->outst.last_burst_time = brst_time;
            stream->outst.wire_cons_smpl = wire_smpl_consumed;
            stream->outst.wire_cons_bytes = wire_buf_off;
            stream->outst.wire_bidx = bidx;
        }
    } while (host_smpl_rem != 0);

    if (nfo) {
        nfo->totsyms  = stream->host_pkt_symbs - host_smpl_rem;
        nfo->totlost  = 0;
        nfo->fsymtime = fsym_time;

        USDR_LOG("DSTR", USDR_LOG_TRACE, "OUT %lld - %d\n", (long long)nfo->fsymtime, nfo->totsyms);
    }

    return 0;
}

static
int _limestr_stream_send(stream_handle_t* str,
                         const char **stream_buffs,
                         unsigned samples,
                         dm_time_t timestamp,
                         unsigned timeout)
{
    int res;
    stream_limesdr_t* stream = (stream_limesdr_t*)str;
    lldev_t dev = stream->base.dev->dev;
    struct lowlevel_ops* ops = lowlevel_get_ops(dev);
    char* dma_buf;

    unsigned host_bps = (stream->burst_host_bytes / stream->burst_symbs);
    unsigned wire_bps = (stream->burst_bytes / stream->burst_symbs);

    unsigned host_buf_off = 0;
    unsigned wire_buf_off = 0;

    unsigned ignoreTimestamp = (((int64_t)timestamp) < 0);

    if (stream->burst_count * stream->burst_symbs < samples) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Frame is too big, max = %dx%d, requested = %d\n",
                 stream->burst_count, stream->burst_symbs, samples);
        return -EINVAL;
    }
    if (samples % stream->tx_sampl_c) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Samples isn't round to %d, requested %d\n", stream->tx_sampl_c, samples);
        return -EINVAL;
    }

    res = ops->send_dma_get(dev, 0,
                            stream->ll_streamo, (void**)&dma_buf, NULL, NULL,
                            timeout);
    if (res < 0)
        return res;

    USDR_LOG("DSTR", USDR_LOG_NOTE, "Send %lld - %d\n", (long long)timestamp, samples);

    for (unsigned s = 0; s < samples; s += stream->block_samples) {
        void* dma_bufs_bias[] = {
            dma_buf + wire_buf_off + LIMESDR_STREAM_HDR,
        };
        const void* strem_bufs_bias[] = {
            stream_buffs[0] + host_buf_off,
        };
        unsigned samples_needed = samples - s;
        if (samples_needed > stream->block_samples)
            samples_needed = stream->block_samples;

        unsigned wire_proc_bytes = samples_needed * wire_bps;
        unsigned host_proc_bytes = samples_needed * host_bps;

        // Fill header
        uint64_t *brst_time = (uint64_t*)(dma_buf + 8 + wire_buf_off);
        *brst_time = timestamp + s;

        uint8_t *brst_ctrl = (uint8_t*)(dma_buf + 0 + wire_buf_off);
        brst_ctrl[0] = ignoreTimestamp << 4;
        brst_ctrl[1] = wire_proc_bytes & 0xFF;
        brst_ctrl[2] = (wire_proc_bytes >> 8) & 0xFF;
        brst_ctrl[3] = 0;
        brst_ctrl[4] = brst_ctrl[5] = brst_ctrl[6] =  brst_ctrl[7] = 0;

        // Convert data
        stream->tf_data(strem_bufs_bias, host_proc_bytes, dma_bufs_bias, wire_proc_bytes);

        wire_buf_off += wire_proc_bytes + LIMESDR_STREAM_HDR;
        host_buf_off += host_proc_bytes;
    }

    stream->rcnt++;
    res = ops->send_dma_commit(dev, 0,
                               stream->ll_streamo, dma_buf, wire_buf_off,
                               &timestamp, sizeof(timestamp));
    return res;
}


static
int _limestr_stream_op(stream_handle_t* str,
                       unsigned command,
                       dm_time_t tm)
{
//    int res;
//    stream_limesdr_t* stream = (stream_limesdr_t*)str;
//    lldev_t dev = stream->base.dev->dev;

//    if (command == USDR_DMS_STOP) {

//    }

    return 0;
}

static
int _limestr_stream_get(stream_handle_t* str, const char* name, int64_t* out_val)
{
    stream_limesdr_t* stream = (stream_limesdr_t*)str;
    if (strcmp(name, "fd") == 0) {
        *out_val = stream->fd;
        return 0;
    }
    return -EINVAL;
}

static
int _limestr_stream_set(stream_handle_t* str, const char* name, int64_t UNUSED in_val)
{
    return -EINVAL;
}

static
int _limestr_stream_stat(stream_handle_t* str, usdr_dms_nfo_t* nfo)
{
    stream_limesdr_t* stream = (stream_limesdr_t*)str;

    nfo->channels = 1;
    nfo->type = stream->ll_streamo == LIMESDR_RX ? USDR_DMS_RX : USDR_DMS_TX;
    nfo->pktbszie = stream->host_pkt_symbs * (stream->burst_host_bytes / stream->burst_symbs);
    nfo->pktsyms = stream->host_pkt_symbs;
    nfo->burst_count = stream->burst_count;

    return 0;
}


static const struct stream_ops s_limestr_ops = {
    .destroy = &_limestr_destroy,
    .op = &_limestr_stream_op,
    .recv = &_limestr_stream_recv,
    .send = &_limestr_stream_send,
    .stat = &_limestr_stream_stat,
    .option_get = &_limestr_stream_get,
    .option_set = &_limestr_stream_set,
};





int create_limesdr_stream(device_t* device,
                          unsigned core_id,
                          const char* dformat,
                          logical_ch_msk_t channels,
                          unsigned pktsyms,
                          unsigned flags,
                          stream_handle_t** outu)
{
    int res;
    lowlevel_stream_params_t sparams;
    lowlevel_ops_t* dops = lowlevel_get_ops(device->dev);
    stream_limesdr_t* strdev;
    stream_t sid;
    char dfmt[256];
    strncpy(dfmt, dformat, sizeof(dfmt));
    struct parsed_data_format pfmt;

    if (stream_parse_dformat(dfmt, &pfmt)) {
        return -EINVAL;
    }

    bool need_fd = (flags & DMS_FLAG_NEED_FD) == DMS_FLAG_NEED_FD;
    const unsigned hwchans = 1;
    const char* sfmt = (pfmt.wire_fmt == NULL) ? pfmt.host_fmt : pfmt.wire_fmt;
    bool cflat = false;
    if (*sfmt == '&') {
        //if (channels != 3)
        //    return -EINVAL;

        sfmt++;
        cflat = true;
    }

    struct bitsfmt bfmt = get_bits_fmt(sfmt);
    const unsigned logicchs = 1; // Only 1 channel is supported at this moment
    transform_info_t funcs;

    if (!bfmt.complex && !cflat) {
        return -EINVAL; // Only complex
    }
    switch (bfmt.bits) {
    case 0:
        // Defaults to 16 bits
        sfmt = SFMT_CI16;
        bfmt.bits = 16;
        bfmt.complex = true;
        break;
    case 12:
    case 16:
        break;
    default:
        return -EINVAL; // Only 12 & 16 bit streaming supported
    }

    ///////////////////////////// FIXUP FOR TIMING
    bfmt.complex = true;

    if (core_id == LIMESDR_RX) {
        funcs = get_transform_fn(sfmt,
                                 pfmt.host_fmt,
                                 hwchans,
                                 logicchs);
    } else {
        funcs = get_transform_fn(pfmt.host_fmt,
                                 sfmt,
                                 logicchs,
                                 hwchans);
    }

    if (funcs.cfunc == NULL || funcs.sfunc == NULL) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "No transform function '%s'->'%s' are available for 1->%d demux\n",
                 sfmt, pfmt.host_fmt, logicchs);
        return -EINVAL;
    }
    if (is_transform_dummy(funcs.cfunc)) {
        USDR_LOG("DSTR", USDR_LOG_INFO, "No transformation!\n");
    }

    const unsigned wire_sample_size = bfmt.bits * (bfmt.complex ? 2 : 1);
    const unsigned burst_size = 4096;
    const unsigned burst_smpl = (burst_size - LIMESDR_STREAM_HDR) * 8 / wire_sample_size ;

    // If wire bust doesn't fit 0.4 of original size so extra buffering would be 20% max.
    // TODO dynamic rescaling
    unsigned burst_count;
    unsigned tx_sampl_c = 0;
    if (core_id == LIMESDR_RX) {
        burst_count = (pktsyms % burst_smpl) ? (2 * pktsyms / burst_smpl / 5) : (pktsyms / burst_smpl);
    } else {
        burst_count = (pktsyms + burst_smpl - 1) /  burst_smpl;

        // rem 16 bytes for 16 bits and 48 bytes for 12 bits
        tx_sampl_c = ((bfmt.bits == 12) ? 48 / 3 / hwchans  : 16 / 4 / hwchans);
        if (pktsyms % tx_sampl_c) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Transmit packet should be round to %d samples, requested %d!\n", tx_sampl_c, pktsyms);
            return -EINVAL;
        }
    }

    if (burst_count == 0)
        burst_count = 1;

    sparams.streamno = core_id;
    sparams.flags = 0;
    sparams.block_size = burst_size * burst_count;
    sparams.buffer_count = 16;                     // TODO: parameter
    sparams.flags = ((need_fd) ? LLSF_NEED_FDPOLL : 0);
    sparams.channels = 0;
    sparams.bits_per_sym = 0;

    res = dops->stream_initialize(device->dev, 0, &sparams, &sid);
    if (res)
        return res;

    strdev = (stream_limesdr_t*)malloc(sizeof(stream_limesdr_t));

    strdev->base.dev = device;
    strdev->base.ops = &s_limestr_ops;
    strdev->ll_streamo = sid;

    strdev->host_pkt_symbs = pktsyms;

    strdev->tf_data = funcs.cfunc;
    strdev->tf_size = funcs.sfunc;

    strdev->burst_count = burst_count;
    strdev->burst_bytes = burst_size - LIMESDR_STREAM_HDR;
    strdev->burst_symbs = strdev->burst_bytes * 8 / wire_sample_size;
    strdev->burst_host_bytes = funcs.sfunc(strdev->burst_bytes, core_id == LIMESDR_RX ? false : true);
    strdev->block_samples = burst_smpl / hwchans;
    strdev->tx_sampl_c = tx_sampl_c;

    USDR_LOG("DSTR", USDR_LOG_ERROR, "Lime stream %d is configured %d bytes packets (%d bursts) with %d samples per burst\n",
             sid, sparams.block_size, strdev->burst_count, strdev->burst_symbs);

    strdev->rcnt = 0;
    strdev->overruns = 0;
    strdev->blk_time_prev = ~0UL;
    strdev->lag_remaining = 0;

    strdev->outst.dma_buf = NULL;
    //strdev->outst.last_burst_time = ~0UL;
    strdev->outst.wire_cons_smpl = 0;
    strdev->outst.wire_cons_bytes = 0;
    strdev->outst.wire_bidx = 0;

    strdev->fd = sparams.underlying_fd;

    *outu = &strdev->base;
    return 0;
}
