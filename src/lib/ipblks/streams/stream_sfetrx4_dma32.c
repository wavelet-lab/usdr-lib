// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "stream_sfetrx4_dma32.h"

#include "dma_rx_32.h"
#include "sfe_rx_4.h"
#include "dma_tx_32.h"
#include "sfe_tx_4.h"

#include "../../xdsp/conv.h"
#include "../../device/device_vfs.h"

#include "../xlnx_bitstream.h"

#define MINIM_FWID_COMPAT   0xd2b10c09

struct stream_stats {
    uint64_t wirebytes;
    uint64_t symbols;
    unsigned pktok;
    unsigned dropped;
};
typedef struct stream_stats stream_stats_t;


struct stream_sfetrx_dma32 {
    struct stream_handle base;

    unsigned type;
    unsigned flags;
    unsigned channels;
    stream_t ll_streamo;

    // Cached values
    unsigned cnf_base;
    unsigned aux_base;
    unsigned cfg_base;

    unsigned pkt_symbs;
    unsigned pkt_bytes;  //Wire bytes (for SISO or MIMO combined)

    unsigned host_bytes; //Host bytes (for SISO or MIMO combined)

    unsigned bps;        // bits per symbol on wire

    conv_function_t tf_data;
    size_function_t tf_size;

    uint32_t cached_samples;
    uint64_t rcnt;
    uint64_t r_ts;

    uint32_t burst_mask;

    stream_stats_t stats;
    int fd;
    unsigned burst_count;
};
typedef struct stream_sfetrx_dma32 stream_sfetrx_dma32_t;

enum usdr_str_tpye {
    USDR_ZCPY_RX,
    USDR_ZCPY_TX,
};

static
int _sfetrx4_destroy(stream_handle_t* str)
{
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;
    lldev_t dev = stream->base.dev->dev;

    USDR_LOG("DSTR", USDR_LOG_DEBUG, "Destroying strem %d\n", stream->ll_streamo);
    int res;

    if (stream->type == USDR_ZCPY_RX) {
        //Grcefull stop
        res = lowlevel_reg_wr32(dev, 0,
                                stream->cnf_base + 1, 0);
        if (res)
            return res;

        res = sfe_rx4_startstop(dev, 0,
                                stream->aux_base, 0, 0);
        if (res)
            return res;
    } else {
        res = lowlevel_reg_wr32(dev, 0,
                                stream->cnf_base + 2, 0);
        if (res)
            return res;
    }

    lowlevel_ops_t* dops = lowlevel_get_ops(dev);
    res = dops->stream_deinitialize(dev, 0, stream->ll_streamo);

    // Cleanup device state
    free(stream);
    return res;
}

static
int _sfetrx4_stream_recv(stream_handle_t* str,
                         char** stream_buffs,
                         unsigned timeout,
                         struct usdr_dms_recv_nfo* nfo)
{
    int res;
    struct lowlevel_ops* ops;
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;
    lldev_t dev = stream->base.dev->dev;

    if (stream->type != USDR_ZCPY_RX)
        return -ENOTSUP;

    uint64_t oob_data[2];
    unsigned oob_size = sizeof(oob_data);
    char* dma_buf;

    if (stream->rcnt == 0) {
        // Issue rx ready, should be put inside
        res = lowlevel_reg_wr32(dev, 0,
                                stream->cnf_base + 1, 4);
        if (res)
            return res;
    }

    ops = lowlevel_get_ops(dev);
    res = ops->recv_dma_wait(dev, 0,
                             stream->ll_streamo,
                             (void**)&dma_buf, &oob_data, &oob_size, timeout);
    if (res < 0)
        return res;

    //if (res > 1) {
    if (oob_data[0] & 0xffffff) {
        unsigned pkt_lost = oob_data[0] & 0xffffff;
        USDR_LOG("UDMS", USDR_LOG_INFO, "Recv %016" PRIx64 ".%016" PRIx64 " EXTRA:%d buf=%p seq=%16" PRIu64 "\n", oob_data[0], oob_data[1], res, dma_buf,
                 stream->rcnt);

        stream->stats.dropped += pkt_lost;
        stream->r_ts += stream->pkt_symbs * pkt_lost;
    } else if ((oob_data[0] >> 32) != stream->burst_mask) {
        USDR_LOG("UDMS", USDR_LOG_INFO, "Recv %016" PRIx64 ".%016" PRIx64 " [%08x] EXTRA:%d buf=%p seq=%16" PRIu64 "\n", oob_data[0], oob_data[1], stream->burst_mask, res, dma_buf,
                stream->rcnt);

    } else {
        USDR_LOG("UDMS", USDR_LOG_DEBUG, "Recv %016" PRIx64 ".%016" PRIx64 " EXTRA:- buf=%p seq=%16" PRIu64 "\n", oob_data[0], oob_data[1], dma_buf,
                 stream->rcnt);
    }

    stream->stats.pktok ++;
    stream->stats.wirebytes += stream->pkt_bytes;
    stream->stats.symbols += stream->pkt_symbs;

    // Data transformation
    stream->tf_data((const void**)&dma_buf, stream->pkt_bytes, (void**)stream_buffs, stream->host_bytes);
    stream->rcnt++;

    if (nfo) {
        nfo->fsymtime = stream->r_ts;
        nfo->totsyms = stream->pkt_symbs;
        nfo->totlost = 0;
        nfo->extra = (oob_size >= 16) ? oob_data[1] : 0;
    }

    stream->r_ts += stream->pkt_symbs;

    // Release DMA buffer
    res = ops->recv_dma_release(dev, 0,
                                stream->ll_streamo, dma_buf);
    if (res)
        return res;

    return 0;
}

static
int _sfetrx4_stream_send(stream_handle_t* str,
                         const char **stream_buffs,
                         unsigned samples,
                         dm_time_t timestamp,
                         unsigned timeout)
{
    int res;
    struct lowlevel_ops* ops;
    void* buffer;
    uint32_t stat[4];
    unsigned stat_sz = sizeof(stat);
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;
    lldev_t dev = stream->base.dev->dev;

    if (stream->type != USDR_ZCPY_TX)
        return -ENOTSUP;
    if (stream->pkt_symbs < samples) {
        //return -EOVERFLOW;

        const char* nstreams[16];
        unsigned host_off = stream->tf_size(stream->pkt_symbs * stream->bps / 8, true) / stream->channels;
        assert(stream->channels <= SIZEOF_ARRAY(nstreams));

        memcpy(nstreams, stream_buffs, sizeof(void*) * stream->channels);
        do {
            unsigned ns = (samples < stream->pkt_symbs) ? samples : stream->pkt_symbs;

            res = _sfetrx4_stream_send(str, nstreams, ns, timestamp, timeout);
            if (res)
                return res;

            for (unsigned i = 0; i < stream->channels; i++) {
                nstreams[i] += host_off;
            }
            if (timestamp < INT64_MAX) {
                timestamp += ns;
            }
            samples -= ns;
        } while (samples > 0);

        return 0;
    }

    ops = lowlevel_get_ops(dev);
    res = ops->send_dma_get(dev, 0,
                             stream->ll_streamo, &buffer, stat, &stat_sz,
                             timeout);
    if (res < 0)
        return res;

    uint32_t wire_bytes = stream->channels * samples * stream->bps / 8;
    uint32_t host_bytes = stream->tf_size(wire_bytes, true);

    stream->stats.wirebytes += wire_bytes;
    stream->stats.symbols += samples;


    if (stat_sz > 0) {
        // axis_stat_data      : { filling_bn_uclk[5:4], dma_bufno_written_reg,  filling_bn_uclk[3:2], outnum_cleared, filling_bn_uclk[1:0], dma_bufno_reg,    tx_running , fifo_addr_full, dma_state }
        // axis_stat_m_data    : { delayed_bursts, buffer_req_in_fly[1:0], debug_fe_state, filling_buf_no, ts_rd_addr_reg };
        // axis_stat_ts_data   : { TIMESTAMP }
        // axis_stat_cpl_data  : { last_late_samples, fifo_min_space[3:0], 3'b000, tag_fifo_used[4:0] }


        // Understanding counters            Incrementing when
        // dma_bufno_written_reg -- USB/PCIe Buffer metadata posted
        // dma_bufno             --     PCIe Buffer all MemRd requests are sent
        // filling_bn_uclk       -- USB/PCIe Beffer data has been placed into FIFO RAM
        // outnum_cleared        -- USB/PCIe Buffer has been completly played out (available for reuse)
        unsigned filling_bn_uclk = ((stat[0] >> 30) << 4) | (((stat[0] >> 22) & 0x3) << 2) | (((stat[0] >> 14) & 0x3));
        unsigned dma_bufno_written_reg = (stat[0] >> 24) & 0x3f;
        unsigned outnum_cleared = (stat[0] >> 16) & 0x3f;
        unsigned dma_bufno_reg = (stat[0] >> 8) & 0x3f;

        unsigned delayd = stat[1] >> 16;

        USDR_LOG("UDMS", USDR_LOG_NOTE, "Send stat %d -- %08x.%08x.%08x.%08x -- HOST:%d WIRE:%d\n"
                 "    Buff States (Post/MemRD/FIFO/Cleared) %2d/%2d/%2d/%2d     Running:%d Full:%2d Sate:%d \n"
                 "    Delayd: %d FE:%d FBNO:%2d TSRDADDR:%2d -- FE_TS %9u -- MIN_FIFO: %d\n",
                 stat_sz, stat[0], stat[1], stat[2], stat[3], host_bytes, wire_bytes,
                 dma_bufno_written_reg, dma_bufno_reg, filling_bn_uclk, outnum_cleared, (stat[0] & 0x80) ? 1 : 0, (stat[0] >> 3) & 0xf, stat[0] & 0x7,
                 stat[1] >> 16,  (stat[1] >> 12) & 0x3, (stat[1] >> 6) & 0x3f, stat[1] & 0x3f,
                 stat[2], (stat[3] >> 8) & 0xf);


        if (stream->stats.dropped != delayd)
            stream->stats.dropped = delayd;
        else
            stream->stats.pktok ++;
    } else {
        stream->stats.pktok ++;
    }

    stream->tf_data((const void**)stream_buffs, host_bytes, &buffer, wire_bytes);
    stream->rcnt++;

    uint64_t oob[1] = { timestamp };
    res = ops->send_dma_commit(dev, 0,
                               stream->ll_streamo, buffer, wire_bytes,
                               &oob, sizeof(oob));
    if (res)
        return res;

    return 0;
}


static int _sfetrx4_op(stream_handle_t* str,
                       unsigned command,
                       dm_time_t tm)
{
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;
    lldev_t dev = stream->base.dev->dev;
    int res;
    bool start;
    switch (command) {
    case USDR_DMS_START:
    case USDR_DMS_START_AT:
        start = true;
        break;
    default:
        USDR_LOG("UDMS", USDR_LOG_INFO, "Stream[%d] STOP; STATS bytes = %" PRIu64 ", samples = %" PRIu64 ", dropped/rcvd = %d/%d\n",
                stream->ll_streamo, stream->stats.wirebytes, stream->stats.symbols, stream->stats.dropped, stream->stats.pktok);
        start = false;
    }

    if (stream->type == USDR_ZCPY_RX) {
        // Enable DMA first
        res = lowlevel_reg_wr32(dev, 0,
                                stream->cnf_base + 1, start ? 1 : 0);
        if (res)
            return res;

        res = sfe_rx4_startstop(dev, 0,
                                stream->aux_base, tm, start);
        if (res)
            return res;
    } else {
        res = sfe_tx4_ctl(dev, 0, stream->cnf_base,
                          stream->channels != 1, false, start);
        if (res)
            return res;

#if 0
        res = lowlevel_reg_wr32(stream->base.dev, 0,
                                stream->cnf_base + 2, 0);
        if (res)
            return res;

        if (start) {
            res = lowlevel_reg_wr32(stream->base.dev, 0,
                                    stream->cnf_base + 2, 1<<7);

            // Enable DMA first (16bit, MIMO)
            uint32_t cmd = stream->channels == 1 ? ((1 << 3) | (1 << 8)) : 0;
            res = lowlevel_reg_wr32(stream->base.dev, 0,
                                    stream->cnf_base + 2, cmd | (start ? 3 : 0));
            if (res)
                return res;
        }
#endif
    }

    return 0;
}


static
int _sfetrx4_option_get(stream_handle_t* str, const char* name, int64_t* out_val)
{
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;
    if (strcmp(name, "fd") == 0) {
        *out_val = stream->fd;
        return 0;
    }
    return -EINVAL;
}

static
int _sfetrx4_option_set(stream_handle_t* str, const char* name, int64_t UNUSED in_val)
{
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;
    if (strcmp(name, "ready") == 0) {
        if (stream->type != USDR_ZCPY_RX)
            return -ENOTSUP;

        // Issue rx ready, should be put inside
        return lowlevel_reg_wr32(stream->base.dev->dev, 0,
                                 stream->cnf_base + 1, 4);
    }
    return -EINVAL;
}

static
int _sfetrx4_stat(stream_handle_t* str, usdr_dms_nfo_t* nfo)
{
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;

    if (stream->type == USDR_ZCPY_RX) {
        nfo->type = USDR_DMS_RX;
        nfo->channels = stream->channels;
        nfo->pktbszie = stream->host_bytes / nfo->channels;
        nfo->pktsyms = stream->pkt_symbs;
        nfo->burst_count = stream->burst_count;
    } else if (stream->type == USDR_ZCPY_TX) {
        nfo->type = USDR_DMS_TX;
        nfo->channels = stream->channels;
        nfo->pktbszie = stream->host_bytes / nfo->channels;
        nfo->pktsyms = stream->pkt_symbs;
        nfo->burst_count = stream->burst_count;
    } else {
        return -ENOTSUP;
    }

    return 0;
}

static const struct stream_ops s_sfetr4_dma32_ops = {
    .destroy = &_sfetrx4_destroy,
    .op = &_sfetrx4_op,
    .recv = &_sfetrx4_stream_recv,
    .send = &_sfetrx4_stream_send,
    .stat = &_sfetrx4_stat,
    .option_get = &_sfetrx4_option_get,
    .option_set = &_sfetrx4_option_set,
};


int parse_sfetrx4(const char* dformat, logical_ch_msk_t channels, unsigned pktsyms,
                  struct sfetrx4_config *out)
{
    struct stream_config sc, sc_b;
    unsigned logicchs = 0;
    unsigned cfgchs = 0;
    bool bifurcation = true;
    struct parsed_data_format pfmt;

    strncpy(out->dfmt, dformat, sizeof(out->dfmt));
    if (stream_parse_dformat(out->dfmt, &pfmt)) {
        return -EINVAL;
    }

    for (logical_ch_msk_t b = 1; b != 0; b <<= 1) {
        if (channels & b)
            cfgchs++;
    }

    sc.burstspblk = 0;
    sc.chmsk = channels;
    sc.sfmt = (pfmt.wire_fmt == NULL) ? pfmt.host_fmt : pfmt.wire_fmt;
    sc.spburst = pktsyms;

    if (*sc.sfmt == '&') {
        logicchs = 1;
        sc.sfmt++;
    } else {
        logicchs = cfgchs;
    }

    // Bifurcation check
    sc_b = sc;
    struct bitsfmt bfmt = get_bits_fmt(sc.sfmt);
    if ((bfmt.complex) && (sc.chmsk == 1 || sc.chmsk == 2)) {
        sc_b.chmsk = 3;
    } else if ((!bfmt.complex) && (sc.chmsk == 0x3 || sc.chmsk == 0xc)) {
        sc_b.chmsk = 15;
    } else {
        bifurcation = false;
    }
    if (sc.spburst % 2) {
        bifurcation = false;
    } else {
        sc_b.spburst /= 2;
    }

    out->sc = sc;
    out->sc_b = sc_b;
    out->cfgchs = cfgchs;
    out->logicchs = logicchs;
    out->bifurcation_valid = bifurcation;

    return 0;
}

static int initialize_stream_rx_32(device_t* device,
                                   logical_ch_msk_t channels,
                                   unsigned pktsyms,
                                   unsigned fe_fifobsz,
                                   unsigned fe_base,
                                   unsigned sx_base,
                                   unsigned sx_cfg_base,
                                   struct parsed_data_format pfmt,
                                   stream_sfetrx_dma32_t** outu,
                                   bool need_fd,
                                   bool need_tx_stat,
                                   bool data_lane_bifurcation)
{
    int res;
    stream_sfetrx_dma32_t* strdev;

    res = dma_rx32_reset(device->dev, 0, sx_base, sx_cfg_base);
    if (res)
        return res;

    struct stream_config sc;
    struct fifo_config fc;
    unsigned logicchs = 0;
    for (logical_ch_msk_t b = 1; b != 0; b <<= 1) {
        if (channels & b)
            logicchs++;
    }

    sc.burstspblk = 0;
    sc.chmsk = channels;
    sc.sfmt = (pfmt.wire_fmt == NULL) ? pfmt.host_fmt : pfmt.wire_fmt;
    sc.spburst = pktsyms;

    if (*sc.sfmt == '&') {
        logicchs = 1;
        sc.sfmt++;
    }

    res = sfe_rx4_check_format(&sc);
    if (res) {
        if (pfmt.wire_fmt != NULL) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Unsupported wire format '%s' by the core\n",
                     sc.sfmt);
            return res;
        }

        // Wire format not supported, need transform function
        // suppose i16 but maintain complex / real format
        sc.sfmt = (*pfmt.host_fmt == 'C' || *pfmt.host_fmt == 'c') ? "ci16" : "i16";
    }

    //Find transform function
    transform_info_t funcs = get_transform_fn(sc.sfmt,
                                              pfmt.host_fmt,
                                              1,
                                              logicchs);
    if (funcs.cfunc == NULL || funcs.sfunc == NULL) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "No transform function '%s'->'%s' are available for 1->%d demux\n",
                 sc.sfmt, pfmt.host_fmt, logicchs);
        return -EINVAL;
    }
    if (is_transform_dummy(funcs.cfunc)) {
        USDR_LOG("DSTR", USDR_LOG_INFO, "No transformation!\n");
    }

    if (data_lane_bifurcation) {
        struct bitsfmt bfmt = get_bits_fmt(sc.sfmt);
        if ((bfmt.complex) && (sc.chmsk == 1 || sc.chmsk == 2)) {
            sc.chmsk = 3;
        } else if ((!bfmt.complex) && (sc.chmsk == 0x3 || sc.chmsk == 0xc)) {
            sc.chmsk = 15;
        } else {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Bifurcation is only valid for single channel complex\n");
            return -EINVAL;
        }
        if (sc.spburst % 2) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "In Bifurcation number of samples should be even\n");
            return -EINVAL;
        }

        sc.spburst /= 2;
    }

    res = sfe_rx4_configure(device->dev, 0, fe_base, fe_fifobsz, &sc, &fc);
    if (res)
        return res;

    need_tx_stat = 1; // Enable stats in all cases
    res = dma_rx32_configure(device->dev, 0, sx_cfg_base, &fc, (need_tx_stat) ? ENABLE_TX_STATS : 0);
    if (res)
        return res;

    res = sfetrx4_stream_sync(device, NULL, 0, "off");
    if (res)
        return res;

    lowlevel_stream_params_t sparams;
    stream_t sid;
    lowlevel_ops_t* dops = lowlevel_get_ops(device->dev);

    sparams.streamno = 0;
    sparams.flags = 0;
    sparams.block_size = fc.bpb * fc.burstspblk;
    sparams.buffer_count = 32;
    sparams.flags = ((need_fd) ? LLSF_NEED_FDPOLL : 0) | (need_tx_stat ? LLSF_EXT_STAT : 0);
    sparams.channels = 0;
    sparams.bits_per_sym = 0;

    sparams.underlying_fd = -1;
    res = dops->stream_initialize(device->dev, 0, &sparams, &sid);
    if (res)
        return res;

    strdev = (stream_sfetrx_dma32_t*)malloc(sizeof(stream_sfetrx_dma32_t));
    //usdr_dmo_init(&strdev->obj_stream, &s_dms_ops);
    //strdev->parent = device;
    strdev->base.dev = device;
    strdev->base.ops = &s_sfetr4_dma32_ops;

    strdev->type = USDR_ZCPY_RX;
    strdev->flags = 0;
    strdev->channels = logicchs;
    strdev->ll_streamo = sid;

    strdev->cnf_base = sx_base;
    strdev->aux_base = fe_base;
    strdev->cfg_base = sx_cfg_base;

    strdev->pkt_symbs = pktsyms;
    strdev->pkt_bytes = sparams.block_size;
    strdev->host_bytes = funcs.sfunc(sparams.block_size, false);

    strdev->bps = fc.bpb * 8 / sc.spburst;

    strdev->tf_data = funcs.cfunc;
    strdev->tf_size = funcs.sfunc;

    strdev->cached_samples = ~0u;
    strdev->rcnt = 0;
    strdev->r_ts = 0; // Start timestamp

    strdev->stats.wirebytes = 0;
    strdev->stats.symbols = 0;
    strdev->stats.pktok = 0;
    strdev->stats.dropped = 0;

    strdev->fd = sparams.underlying_fd;

    strdev->burst_mask = ((((uint64_t)1U) << fc.burstspblk) - 1) << (32 - fc.burstspblk);
    strdev->burst_count = fc.burstspblk;
    *outu = strdev;
    return 0;
}

enum {
    MAX_TX_BUFF = 32768
};



static int initialize_stream_tx_32(device_t* device,
                                   logical_ch_msk_t channels,
                                   unsigned pktsyms,
                                   //unsigned fe_fifobsz,
                                   //unsigned fe_base,
                                   unsigned sx_base,
                                   unsigned sx_cfg_base,
                                   struct parsed_data_format pfmt,
                                   stream_sfetrx_dma32_t** outu,
                                   bool need_fd,
                                   bool data_lane_bifurcation)
{
    int res;
    stream_sfetrx_dma32_t* strdev;

    struct stream_config sc;
    unsigned logicchs = 0;
    for (logical_ch_msk_t b = 1; b != 0; b <<= 1) {
        if (channels & b)
            logicchs++;
    }

    sc.burstspblk = 0;
    sc.chmsk = channels;
    sc.sfmt = (pfmt.wire_fmt == NULL) ? pfmt.host_fmt : pfmt.wire_fmt;
    sc.spburst = pktsyms;

    if (logicchs > 2)
        return -EINVAL;

    uint64_t fwid;
    res = usdr_device_vfs_obj_val_get_u64(device, "/dm/revision", &fwid);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "Unable to check comatability firmware!\n");
    }
    if (get_xilinx_rev_h(fwid & 0xffffffff) < get_xilinx_rev_h(MINIM_FWID_COMPAT)) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "You're running outdated firmware, please update! CurrentID=%08x MinimalID=%08x\n",
                 (uint32_t)(fwid & 0xffffffff),
                 MINIM_FWID_COMPAT);
        return -ECONNRESET;
    }

    unsigned bits_per_single_sym = 32; // We support I/Q 16 bit for now only
    unsigned hardware_channels = logicchs;
    if (*sc.sfmt == '&') {
        logicchs = 1;
        sc.sfmt++;
    }

    if (data_lane_bifurcation) {
        struct bitsfmt bfmt = get_bits_fmt(sc.sfmt);
        if ((bfmt.complex) && (sc.chmsk == 1 || sc.chmsk == 2)) {
            sc.chmsk = 3;
        } else {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Bifurcation is only valid for single channel complex");
            return -EINVAL;
        }
    }

    res = sfe_tx4_check_format(&sc);
    if (res) {
        if (pfmt.wire_fmt != NULL) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "TX Stream:Unsupported wire format '%s' by the core\n",
                     sc.sfmt);
            return res;
        }

        // Wire format not supported, need transform function
        // suppose i16 but maintain complex / real format
        sc.sfmt = (*pfmt.host_fmt == 'C' || *pfmt.host_fmt == 'c') ? "ci16" : "i16";
    }

    //Find transform function
    transform_info_t funcs = get_transform_fn(pfmt.host_fmt,
                                              sc.sfmt,
                                              logicchs,
                                              1);
    if (funcs.cfunc == NULL || funcs.sfunc == NULL) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "TX Stream: No transform function '%s'->'%s' are available for %d->1 mux\n",
                 pfmt.host_fmt, sc.sfmt, logicchs);
        return -EINVAL;
    }
    if (is_transform_dummy(funcs.cfunc)) {
        USDR_LOG("DSTR", USDR_LOG_INFO, "TX Stream: No transformation!\n");
    }

    res = sfetrx4_stream_sync(device, NULL, 0, "off");
    if (res)
        return res;

    lowlevel_stream_params_t sparams;
    stream_t sid;
    lowlevel_ops_t* dops = lowlevel_get_ops(device->dev);
    unsigned max_mtu = sfe_tx4_mtu_get(&sc);

    sparams.streamno = 1;
    sparams.flags = 1;
    sparams.block_size = pktsyms * hardware_channels * bits_per_single_sym / 8;
    sparams.buffer_count = 32;
    sparams.flags = (need_fd) ? LLSF_NEED_FDPOLL : 0;
    sparams.channels = hardware_channels;
    sparams.bits_per_sym = hardware_channels * bits_per_single_sym;

    if (sparams.block_size > max_mtu) {
        USDR_LOG("DSTR", USDR_LOG_CRITICAL_WARNING, "TX Stream maximum MTU is %d bytes, we need %d to deliver %d samples blocksize!\n",
                 max_mtu, sparams.block_size, pktsyms);
        return -EINVAL;
    }

    res = dops->stream_initialize(device->dev, 0, &sparams, &sid);
    if (res)
        return res;

    if (pktsyms == 0) {
        if (sparams.out_mtu_size > max_mtu)
            sparams.out_mtu_size = max_mtu;

        pktsyms = 8 * sparams.out_mtu_size / (hardware_channels * bits_per_single_sym);
        USDR_LOG("DSTR", USDR_LOG_INFO, "TX Stream: No desired packetsize were set, assuminng maximum %d sps (%d blocksize)\n",
                 pktsyms, (unsigned)sparams.out_mtu_size);

        sparams.block_size = sparams.out_mtu_size;
    }

    strdev = (stream_sfetrx_dma32_t*)malloc(sizeof(stream_sfetrx_dma32_t));

    strdev->base.dev = device;
    strdev->base.ops = &s_sfetr4_dma32_ops;

    strdev->type = USDR_ZCPY_TX;
    strdev->flags = 0;
    strdev->channels = logicchs;
    strdev->ll_streamo = sid;

    strdev->cnf_base = sx_base;
    strdev->aux_base = 0;
    strdev->cfg_base = sx_cfg_base;

    strdev->pkt_symbs = pktsyms;
    strdev->pkt_bytes = sparams.block_size;
    strdev->host_bytes = funcs.sfunc(sparams.block_size, true);

    strdev->bps = bits_per_single_sym; // 16bit I/Q

    strdev->tf_data = funcs.cfunc;
    strdev->tf_size = funcs.sfunc;

    strdev->cached_samples = ~0u;
    strdev->rcnt = 0;
    strdev->r_ts = 0; // Start timestamp

    strdev->stats.wirebytes = 0;
    strdev->stats.symbols = 0;
    strdev->stats.pktok = 0;
    strdev->stats.dropped = 0;

    strdev->fd = sparams.underlying_fd;

    strdev->burst_mask = 0;
    strdev->burst_count = 0; //TODO: fill actual maximum burst count
    *outu = strdev;
    return 0;
}


int create_sfetrx4_stream(device_t* device,
                          unsigned core_id,
                          const char* dformat,
                          logical_ch_msk_t channels,
                          unsigned pktsyms,
                          unsigned flags,
                          unsigned sx_base,
                          unsigned sx_cfg_base,
                          unsigned fe_fifobsz,
                          unsigned fe_base,
                          stream_handle_t** outu,
                          unsigned *hw_chans_cnt)
{
    bool need_fd = (flags & DMS_FLAG_NEED_FD) == DMS_FLAG_NEED_FD;
    bool need_tx_stat = (flags & DMS_FLAG_NEED_TX_STAT) == DMS_FLAG_NEED_TX_STAT;
    bool bifurcation = (flags & DMS_FLAG_BIFURCATION) == DMS_FLAG_BIFURCATION;
    char dfmt[256];
    int res;

    strncpy(dfmt, dformat, sizeof(dfmt));
    struct parsed_data_format pfmt;
    if (stream_parse_dformat(dfmt, &pfmt)) {
        return -EINVAL;
    }

    switch (core_id) {
    case CORE_SFERX_DMA32_R0:
        res = initialize_stream_rx_32(device, channels, pktsyms,
                                      fe_fifobsz, fe_base,
                                      sx_base, sx_cfg_base, pfmt,
                                      (stream_sfetrx_dma32_t** )outu,
                                      need_fd, need_tx_stat, bifurcation);
        break;
    case CORE_SFETX_DMA32_R0:
        res = initialize_stream_tx_32(device, channels, pktsyms,
                                       sx_base, sx_cfg_base, pfmt,
                                       (stream_sfetrx_dma32_t** )outu,
                                       need_fd, bifurcation);
        break;
    default:
        return -EINVAL;
    }

    if (res)
        return res;

    *hw_chans_cnt = (*(stream_sfetrx_dma32_t** )outu)->channels;
    return 0;
}

enum {
    ST_STOP         = 0,
    ST_ONEPPS       = 1,
    ST_ADCACT       = 2,
    ST_DACACT       = 3,
    ST_ADCANDDACACT = 4,
    ST_ADCORDACACT  = 5,
    ST_DELAYADCDAC  = 6,
    ST_FREERUN      = 7,
    ST_SYSREF_GEN   = 8,
};

int sfetrx4_stream_sync(device_t* device,
                        stream_handle_t** pstr, unsigned scount, const char* synctype)
{
    int res;
    int retimer_base = 14; //TODO: get from device
    stream_sfetrx_dma32_t** pstream = (stream_sfetrx_dma32_t**)pstr;

    if (synctype == NULL || !strcmp(synctype, "none")) {
        res = lowlevel_reg_wr32(device->dev, 0, retimer_base, (1u << 31) | (ST_FREERUN << 16));
    } else if (!strcmp(synctype, "sysref") || !strcmp(synctype, "1pps")) {
        res = lowlevel_reg_wr32(device->dev, 0, retimer_base, (1u << 31) | (ST_ONEPPS << 16));
    } else if (!strcmp(synctype, "sysref+gen")) {
        res = lowlevel_reg_wr32(device->dev, 0, retimer_base, (1u << 31) | ((ST_ONEPPS | ST_SYSREF_GEN) << 16));
    } else if (!strcmp(synctype, "rx")) {
        res = lowlevel_reg_wr32(device->dev, 0, retimer_base, (1u << 31) | (ST_ADCACT << 16));
    } else if (!strcmp(synctype, "tx")) {
        res = lowlevel_reg_wr32(device->dev, 0, retimer_base, (1u << 31) | (ST_DACACT << 16));
    } else if (!strcmp(synctype, "any")) {
        res = lowlevel_reg_wr32(device->dev, 0, retimer_base, (1u << 31) | (ST_ADCORDACACT << 16));
    } else if (!strcmp(synctype, "all")) {
        if (scount > 2 || scount < 1)
            return -EINVAL;

        bool rxsync = false;
        bool txsync = false;

        for (unsigned i = 0; i < scount; i++) {
            if (!pstream[i])
                continue;

            if (pstream[i]->type == USDR_ZCPY_RX)
                rxsync = true;
            else if (pstream[i]->type == USDR_ZCPY_TX)
                txsync = true;
        }

        unsigned cmd =
                (rxsync && txsync) ? ST_DELAYADCDAC :
                (rxsync) ? ST_ADCACT :
                (txsync) ? ST_DACACT : ST_FREERUN;

        res = lowlevel_reg_wr32(device->dev, 0, retimer_base, (1u << 31) | (cmd << 16));
    } else if (!strcmp(synctype, "off")) {
        res = lowlevel_reg_wr32(device->dev, 0, retimer_base, (1u << 31) | (ST_STOP << 16));
    } else {
        res = -EINVAL;
    }

    return res;
}




