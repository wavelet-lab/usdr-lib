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
    uint64_t pktok;
    uint64_t fe_drop;
    uint64_t dma_drop;
};
typedef struct stream_stats stream_stats_t;

struct extxcfg_cache {
    uint32_t reg_0;
    uint32_t reg_1;
    uint32_t reg_2;
};
typedef struct extxcfg_cache extxcfg_cache_t;

void extxcfg_cache_init(extxcfg_cache_t* c)
{
    c->reg_0 = 0;
    c->reg_1 = 0;
    c->reg_2 = ~1u;
}

struct stream_sfetrx_dma32 {
    struct stream_handle base;

    unsigned type;
    unsigned flags;
    unsigned channels;   // Number of logical channels (that user requested)
    unsigned hwchannels; // Number of internal channels (i.e. bifurcated channels forming logical channels)
    stream_t ll_streamo;

    // Cached values
    unsigned cnf_base;
    unsigned sync_base;  // TODO: for compatibility with OLD APIs
    unsigned cnfrd_base; // Reabdack address for OLD API

    unsigned pkt_symbs;  // Total number of symbols in a transaction (all bursts)
    unsigned pkt_bytes;  // Wire bytes for a transaction (excl. packing overhead)
    unsigned host_bytes; // Host bytes for a transaction
    unsigned wire_bps;   // bits per symbol on wire (for complex symbols both components account)

    conv_function_t tf_data;
    size_function_t tf_size;

    uint32_t cached_samples;
    uint64_t rcnt;
    uint64_t r_ts;

    uint32_t burst_mask;

    stream_stats_t stats;
    int fd;
    unsigned burst_count;
    unsigned burst_align_bytes; // Burst align to this byte boundary

    uint8_t  fe_old_tx_mute; // keep OLD TX FE in sync with host state
    uint8_t  fe_old_tx_swap; // keep OLD TX FE in sync with host state
    unsigned fe_chans;       // Number of active channels in frontend
    unsigned fe_complex;     // Compex data streaming
    union {
        sfe_cfg_t srx4;
    } storage;

    extxcfg_cache_t cstx4;
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

        res = sfe_rx4_startstop(&stream->storage.srx4, false);
        if (res)
            return res;
    } else {
        res = lowlevel_reg_wr32(dev, 0,
                                stream->sync_base, 0);
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
        // TODO: Issue rx ready, should be put inside
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

    if (oob_data[0] & 0xffffff) {
        unsigned pkt_lost = oob_data[0] & 0xffffff;
        USDR_LOG("UDMS", USDR_LOG_INFO, "Recv %016" PRIx64 ".%016" PRIx64 " EXTRA:%d buf=%p seq=%16" PRIu64 "\n", oob_data[0], oob_data[1], res, dma_buf,
                 stream->rcnt);

        stream->stats.fe_drop += pkt_lost;
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
        nfo->totlost = stream->stats.fe_drop;
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

static int _extx_burstup(unsigned total_samples, unsigned brst_samples_max, unsigned* plgbrst)
{
    unsigned lgbursts = 0;

    for (unsigned bcnt; lgbursts < 4; lgbursts++) {
        bcnt = 1 << lgbursts;

        if (((total_samples / bcnt) <= brst_samples_max) && (total_samples % bcnt == 0)) {
            *plgbrst = lgbursts;
            return 0;
        }
    }
    return -EINVAL;
}

struct txcore_statistics
{
    uint8_t usrbuf_completed;
    uint8_t usrbuf_posted;
    uint8_t usrbuf_aired;
    uint8_t usrbuf_requested;
    unsigned pcietags;
    unsigned fifo_used;
    unsigned drop_fe;
    unsigned drop_dma;
    unsigned timestamp_low;
    unsigned bursts_sent;
    unsigned stat_cpl_nodata;
    // Flags
    uint8_t fifo_full;
    uint8_t stat_dmareq_ovf;
    uint8_t notlp;
    uint8_t core_ready;
};
typedef struct txcore_statistics txcore_statistics_t;

void parse_txcore_stat(uint32_t stat[4], txcore_statistics_t* s)
{
    //              dma_state = { stat_dmareq_ovf, stat_notlp, core_ready }
    // stat     : { usrbuf_completed[5:4], usrbuf_posted,  usrbuf_completed[3:2], usrbuf_aired, usrbuf_completed[1:0], usrbuf_requested, tx_running, 2'b01, tx_buffprecharged, tran_usb_active, dma_state }
    // stat_m   : { stat_pcie_tags_avail[7:0], stat_fifo_used[7:0], dropped_bursts[7:0], m_dma_late[7:0] };
    // stat_ts  : { TIMESTAMP }
    // stat_cpl : { bursts_sent, stat_cpl_nodata[7:0], fifo_full, stat_dmareq_ovf, 6'h00 }
    //
    // usbstat[15:0]   = { fifo_remaining[TX_RAM_ADDR_WIDTH:8], fetx_mode_format, usbs_burst_busy, drop_packet, state[2:0] };
    // usbstat[31:16]  = dropped;
    // usbstat[47:32]  = pkt_pshd;
    //

    // Understanding counters (PCIe mode)
    // usrbuf_posted     --     PCIe Buffer metadata posted
    // usrbuf_requested  --     PCIe Buffer all MemRd requests are sent
    // usrbuf_completed  --     PCIe Beffer data has been placed into FIFO RAM
    // usrbuf_aired      --     PCIe Buffer has been completly played out (available for reuse)

    bool usb = (stat[0] & 0x8);

    s->notlp = (stat[0] >> 1) & 1;
    s->core_ready = (stat[0] >> 0) & 1;

    if (usb) {
        s->usrbuf_completed = 0;
        s->usrbuf_posted = 0;
        s->usrbuf_aired = 0;
        s->usrbuf_requested = 0;

        s->pcietags = 0;
        s->fifo_used = 0; //((stat[1]) & 0xffff) >> 8;
        s->drop_fe = (stat[0] >> 8) & 0xff;
        s->drop_dma = (stat[1] >> 16) & 0xffff; //dropped
        s->timestamp_low =0;
        s->bursts_sent = (stat[3] >> 16) & 0xffff;
        s->stat_cpl_nodata = (stat[3] >> 0) & 0xffff; // pkt_pshd
        s->fifo_full = 0;
        s->stat_dmareq_ovf = 0;

        s->core_ready = (stat[0] >> 4) & 1; // Precharged
    } else {
        s->usrbuf_completed = ((stat[0] >> 30) << 4) | (((stat[0] >> 22) & 0x3) << 2) | (((stat[0] >> 14) & 0x3));
        s->usrbuf_posted = (stat[0] >> 24) & 0x3f;
        s->usrbuf_aired = (stat[0] >> 16) & 0x3f;
        s->usrbuf_requested = (stat[0] >> 8) & 0x3f;

        s->pcietags = (stat[1] >> 24) & 0xff;
        s->fifo_used = (stat[1] >> 16) & 0xff;
        s->drop_fe = (stat[1] >> 8) & 0xff;
        s->drop_dma = (stat[1] >> 0) & 0xff;
        s->timestamp_low = stat[2];
        s->bursts_sent = (stat[3] >> 16) & 0xffff;
        s->stat_cpl_nodata = (stat[3] >> 8) & 0xff;
        s->fifo_full = (stat[3] >> 7) & 1;
        s->stat_dmareq_ovf = (stat[3] >> 6) & 1;
    }
}


static
int _sfetrx4_stream_send(stream_handle_t* str,
                         const char **stream_buffs,
                         unsigned samples,
                         dm_time_t timestamp,
                         unsigned timeout,
                         usdr_dms_send_stat_t* ostat)
{
    int res;
    struct lowlevel_ops* ops;
    void* buffer;
    uint32_t stat[4];
    unsigned stat_sz = sizeof(stat);
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;
    lldev_t dev = stream->base.dev->dev;
    unsigned lgbursts = 0;
    unsigned brst_align = stream->burst_align_bytes - 1;
    unsigned brst_samples = stream->pkt_symbs / stream->burst_count;

    if (stream->type != USDR_ZCPY_TX)
        return -ENOTSUP;

    if (stream->storage.srx4.cfg_fecore_id == CORE_EXFETX_DMA32_R0) {
        res = _extx_burstup(samples, brst_samples, &lgbursts);
        if (res)
            return res;

        samples /= (1 << lgbursts);
    }

    if (brst_samples < samples) {
        const char* nstreams[16];
        unsigned host_off = stream->tf_size(brst_samples * stream->wire_bps / 8, true) / stream->channels;
        assert(stream->channels <= SIZEOF_ARRAY(nstreams));

        memcpy(nstreams, stream_buffs, sizeof(void*) * stream->channels);
        do {
            unsigned ns = (samples < brst_samples) ? samples : brst_samples;

            res = _sfetrx4_stream_send(str, nstreams, ns, timestamp, timeout, ostat);
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
    res = ops->send_dma_get(dev, 0, stream->ll_streamo, &buffer, stat, &stat_sz, timeout);
    if (res < 0) {
        if (res == -ETIMEDOUT) {
            txcore_statistics_t st;
            uint32_t stat[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
            res = lowlevel_reg_rdndw(dev, 0, stream->cnfrd_base, stat, 4);
            if (res) {
                return res;
            }
            parse_txcore_stat(stat, &st);

            USDR_LOG("UDMS", USDR_LOG_ERROR, "Send timed out (Pstd/Reqd/Cpld/Aired) %2d/%2d/%2d/%2d TAGS:%d FIFO:%d DROP_DMA:%d DROP_FE:%d OVF:%d FULL:%d CPL_NO_DATA:%d FE_SENT:%d READY:%d -- %08x.%08x.%08x.%08x --\n",
                     st.usrbuf_posted, st.usrbuf_requested, st.usrbuf_completed, st.usrbuf_aired,
                     st.pcietags, st.fifo_used, st.drop_dma, st.drop_fe,
                     st.stat_dmareq_ovf, st.fifo_full, st.stat_cpl_nodata, st.bursts_sent, st.core_ready,
                     stat[0], stat[1], stat[2], stat[3]);
            return -ETIMEDOUT;
        }
        return res;
    }

    uint32_t wire_bytes = stream->channels * samples * stream->wire_bps / 8;
    uint32_t host_bytes = stream->tf_size(wire_bytes, true);
    unsigned bursts = 1 << lgbursts;

    stream->stats.wirebytes += wire_bytes * bursts;
    stream->stats.symbols += samples * bursts;

    if (stat_sz > 0) {
        txcore_statistics_t st;
        parse_txcore_stat(stat, &st);

        //uint64_t pfe = stream->stats.fe_drop, pda = stream->stats.dma_drop;
        if ((stream->stats.fe_drop & 0xff) != st.drop_fe) {
            if ((stream->stats.fe_drop & 0xff) > st.drop_fe)
                stream->stats.fe_drop += 0x100;
            stream->stats.fe_drop = (stream->stats.fe_drop & ~((uint64_t)0xff)) | st.drop_fe;
        }
        if ((stream->stats.dma_drop & 0xff) != st.drop_dma) {
            if ((stream->stats.dma_drop & 0xff) > st.drop_dma)
                stream->stats.dma_drop += 0x100;
            stream->stats.dma_drop = (stream->stats.dma_drop & ~((uint64_t)0xff)) | st.drop_dma;
        }

        //unsigned burst_lost = (stream->stats.fe_drop - pfe) + (stream->stats.dma_drop - pda);
        stream->stats.pktok ++;

        USDR_LOG("UDMS", USDR_LOG_DEBUG, "Send stat %d -- %08x.%08x.%08x.%08x -- HOST:%d WIRE:%d\n"
                                        "    Buff (Pstd/Reqd/Cpld/Aired) %2d/%2d/%2d/%2d  DropFE:%" PRId64 " DropDMA:%" PRId64 " TAGS:%d FIFO:%d\n",
                 stat_sz, stat[0], stat[1], stat[2], stat[3], host_bytes, wire_bytes,
                 st.usrbuf_posted, st.usrbuf_requested, st.usrbuf_completed, st.usrbuf_aired,
                 stream->stats.fe_drop, stream->stats.dma_drop, st.pcietags, st.fifo_used);

        if (ostat) {
            ostat->lhwtime = stat[2];
            ostat->opkttime = 0; // TODO
            ostat->ktime = stat[3];
            ostat->underruns = stream->stats.fe_drop + stream->stats.dma_drop;
            ostat->fifo_used = st.fifo_used;
            ostat->reserved[0] = 0;
            ostat->reserved[1] = 0;
            ostat->reserved[2] = 0;
        }
    } else {
        stream->stats.pktok ++;
    }

    size_t wire_len;
    if ((bursts > 1) && (wire_bytes & brst_align)) {
        void* dma_buffer = buffer;
        const void* nstreams[16];
        memcpy(nstreams, stream_buffs, sizeof(void*) * stream->channels);
        unsigned host_off = host_bytes / stream->channels;

        for (unsigned b = 0; b < bursts; b++) {
            stream->tf_data(nstreams, host_bytes, &dma_buffer, wire_bytes);
            for (unsigned i = 0; i < stream->channels; i++) {
                nstreams[i] = (char*)nstreams[i] + host_off;
            }
            dma_buffer = (char*)dma_buffer + ((wire_bytes + brst_align) & ~brst_align);
        }
        wire_len = (char*)dma_buffer - (char*)buffer;
    } else {
        wire_len = wire_bytes * bursts;
        stream->tf_data((const void**)stream_buffs, host_bytes * bursts, &buffer, wire_len);
    }

    USDR_LOG("UDMS", USDR_LOG_DEBUG, "Send %lld [TS:%lld LG:%d BRST:%d]\n",
             (long long)stream->rcnt, (long long)timestamp, lgbursts, (unsigned)wire_len);

    stream->rcnt++;

    uint64_t oob[3] = { timestamp, lgbursts, wire_len };
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
        USDR_LOG("UDMS", USDR_LOG_INFO, "Stream[%d] STOP; STATS bytes = %" PRIu64 ", samples = %" PRIu64 ", dropped_fe/dropped_dma/rcvd = %" PRIu64 "/%" PRIu64 "/%" PRIu64 "\n",
                stream->ll_streamo, stream->stats.wirebytes, stream->stats.symbols, stream->stats.fe_drop, stream->stats.dma_drop, stream->stats.pktok);
        start = false;
    }

    if (stream->type == USDR_ZCPY_RX) {
        // Enable DMA first
        res = lowlevel_reg_wr32(dev, 0,
                                stream->cnf_base + 1, start ? 1 : 0);
        if (res)
            return res;

        res = sfe_rx4_startstop(&stream->storage.srx4, start);
        if (res)
            return res;
    } else {
        // Assuming Compex IQ
        unsigned lgchcnt = (stream->fe_chans == 1) ? 0 :
                           (stream->fe_chans == 2) ? 1 :
                           (stream->fe_chans == 4) ? 2 : 3;
        res = sfe_tx4_ctl(&stream->storage.srx4, stream->sync_base,
                          lgchcnt, stream->fe_old_tx_swap, stream->fe_old_tx_mute,
                          false, start);
        if (res)
            return res;
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
int _sfetrx4_option_set(stream_handle_t* str, const char* name, int64_t in_val)
{
    stream_sfetrx_dma32_t* stream = (stream_sfetrx_dma32_t*)str;
    if (strcmp(name, "ready") == 0) {
        if (stream->type != USDR_ZCPY_RX)
            return -ENOTSUP;

        // Issue rx ready, should be put inside
        return lowlevel_reg_wr32(stream->base.dev->dev, 0,
                                 stream->cnf_base + 1, 4);
    } else if (stream->type == USDR_ZCPY_RX && strcmp(name, "throttle") == 0) {
        bool enable = (in_val & (1 << 16)) ? true : false;
        uint8_t en = in_val >> 8;
        uint8_t skip = in_val;

        return sfe_rx4_throttle(&stream->storage.srx4, enable, en, skip);
    } else if (strcmp(name, "chmap") == 0) {
        const channel_info_t *new_map = (const channel_info_t *)in_val;

        if (stream->type == USDR_ZCPY_RX) {
            if (stream->storage.srx4.cfg_fecore_id != CORE_EXFERX_DMA32_R0)
                return -ENOTSUP;
        } else if (stream->type == USDR_ZCPY_TX) {
            if (stream->storage.srx4.cfg_fecore_id != CORE_EXFETX_DMA32_R0) {
                unsigned swap_ab_flag;
                int res = fe_tx4_swap_ab_get(stream->fe_chans, new_map, &swap_ab_flag);
                if (res)
                    return res;

                stream->fe_old_tx_swap = swap_ab_flag;
                if (stream->fe_chans == 1 && stream->fe_old_tx_mute != 0) {
                    stream->fe_old_tx_mute = (stream->fe_old_tx_swap) ? 1 : 2;
                }
                return sfe_tx4_upd(&stream->storage.srx4,
                                   stream->sync_base,
                                   stream->fe_old_tx_mute,
                                   stream->fe_old_tx_swap);
            }
        } else {
            return -EINVAL;
        }

        return exfe_trx4_update_chmap(&stream->storage.srx4,
                                      stream->type == USDR_ZCPY_TX,
                                      stream->fe_complex,
                                      (stream->fe_complex ? 2 : 1) * stream->fe_chans,
                                      (const channel_info_t *)in_val);
    } else if (stream->type == USDR_ZCPY_TX && (strcmp(name, "mute") == 0)) {
        if (stream->storage.srx4.cfg_fecore_id != CORE_EXFETX_DMA32_R0) {
            stream->fe_old_tx_mute = in_val & 3;
            return sfe_tx4_upd(&stream->storage.srx4,
                               stream->sync_base,
                               stream->fe_old_tx_mute,
                               stream->fe_old_tx_swap);
        }

        return exfe_tx4_mute(&stream->storage.srx4, in_val);
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


int parse_sfetrx4(const char* dformat, const channel_info_t *channels, unsigned pktsyms, unsigned chcnt,
                  struct sfetrx4_config *out)
{
    struct stream_config sc, sc_b;
    unsigned logicchs = chcnt;
    unsigned cfgchs = 0;
    bool bifurcation = true;
    struct parsed_data_format pfmt;

    strncpy(out->dfmt, dformat, sizeof(out->dfmt));
    if (stream_parse_dformat(out->dfmt, &pfmt)) {
        return -EINVAL;
    }

    sc.burstspblk = 0;
    sc.chcnt = chcnt;
    sc.channels = *channels;
    sc.sfmt = (pfmt.wire_fmt == NULL) ? pfmt.host_fmt : pfmt.wire_fmt;
    sc.spburst = pktsyms;

    if (*sc.sfmt == '&') {
        logicchs = 1;
        sc.sfmt++;
    } else {
        logicchs = cfgchs;
    }

    // TODO: Bifurcation proper set
    sc_b = sc;
    struct bitsfmt bfmt = get_bits_fmt(sc.sfmt);
    if ((bfmt.complex) && (sc.chcnt == 1)) {
        sc_b.chcnt = 2;
    } else if ((!bfmt.complex) && (sc.chcnt == 2)) {
        sc_b.chcnt = 4;
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
                                   unsigned chcount,
                                   channel_info_t *channels,
                                   unsigned pktsyms,
                                   sfe_cfg_t* fecfg,
                                   unsigned sx_base,
                                   unsigned sx_cfg_base,
                                   struct parsed_data_format pfmt,
                                   stream_sfetrx_dma32_t** outu,
                                   bool need_fd,
                                   bool data_lane_bifurcation)
{
    int res;
    stream_sfetrx_dma32_t* strdev;

    res = dma_rx32_reset(device->dev, 0, sx_base);
    if (res)
        return res;

    struct stream_config sc;
    struct fifo_config fc;
    unsigned logicchs = chcount;

    sc.burstspblk = 0;
    sc.chcnt = chcount;
    sc.channels = *channels;
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

    struct bitsfmt bfmt = get_bits_fmt(sc.sfmt);
    if (data_lane_bifurcation) {
        // TODO: proper channel remap, now assuming bifurcation lanes are siblings

        if ((bfmt.complex) && (sc.chcnt == 1)) {
            sc.chcnt = 2;
            sc.channels.ch_map[1] = sc.channels.ch_map[0] + 1;

        } else if ((!bfmt.complex) && (sc.chcnt == 2)) {
            sc.chcnt = 4;

            sc.channels.ch_map[2] = sc.channels.ch_map[0] + 2;
            sc.channels.ch_map[3] = sc.channels.ch_map[1] + 2;
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

    // TODO obtain exfe configuration constants
    res = (fecfg->cfg_fecore_id == CORE_EXFERX_DMA32_R0) ?
        exfe_rx4_configure(fecfg, &sc, &fc) :
        sfe_rx4_configure(fecfg, &sc, &fc);
    if (res)
        return res;

    res = dma_rx32_configure(device->dev, 0, sx_cfg_base, &fc);
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
    sparams.flags = (need_fd) ? LLSF_NEED_FDPOLL : 0;
    sparams.channels = 0;
    sparams.bits_per_sym = 0;

    sparams.underlying_fd = -1;
    sparams.dma_core_id = fecfg->cfg_fecore_id;
    sparams.param = NULL;
    sparams.soft_tx_commit = NULL;

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
    strdev->sync_base = 0;

    strdev->pkt_symbs = pktsyms;
    strdev->pkt_bytes = sparams.block_size;
    strdev->host_bytes = funcs.sfunc(sparams.block_size, false);

    strdev->wire_bps = 8 * strdev->pkt_bytes / strdev->pkt_symbs;

    strdev->tf_data = funcs.cfunc;
    strdev->tf_size = funcs.sfunc;

    strdev->cached_samples = ~0u;
    strdev->rcnt = 0;
    strdev->r_ts = 0; // Start timestamp

    strdev->stats.wirebytes = 0;
    strdev->stats.symbols = 0;
    strdev->stats.pktok = 0;
    strdev->stats.fe_drop = 0;
    strdev->stats.dma_drop = 0;

    strdev->fd = sparams.underlying_fd;

    strdev->burst_mask = ((((uint64_t)1U) << fc.burstspblk) - 1) << (32 - fc.burstspblk);
    strdev->burst_count = fc.burstspblk;
    strdev->burst_align_bytes = fecfg->cfg_dma_align_bytes;

    strdev->fe_chans = sc.chcnt;
    strdev->fe_complex = bfmt.complex;
    strdev->storage.srx4 = *fecfg;

    USDR_LOG("DSTR", USDR_LOG_INFO, "RX: Samples=%d Bps=%d WireBytes=%d HostBytes=%d Bursts=%d\n",
             strdev->pkt_symbs, strdev->wire_bps, strdev->pkt_bytes, strdev->host_bytes, strdev->burst_count);

    *outu = strdev;
    return 0;
}

static int stream_soft_tx_commit(void *param, unsigned sz, const void* oob_ptr, unsigned oob_size)
{
    stream_sfetrx_dma32_t* strdev = (stream_sfetrx_dma32_t*)param;
    uint32_t samples = sz * 8 / (strdev->hwchannels * strdev->wire_bps);
    uint64_t timestamp = (oob_ptr) ? (*(uint64_t*)oob_ptr) : ~0ul;

    return sfe_tx4_push_ring_buffer(strdev->base.dev->dev, 0,
                                    strdev->cnf_base, samples, timestamp);

}

static int stream_soft_extx_commit(void *param, unsigned sz, const void* oob_ptr, unsigned oob_size)
{
    stream_sfetrx_dma32_t* strdev = (stream_sfetrx_dma32_t*)param;
    uint64_t* oob = (uint64_t*)oob_ptr;
    uint32_t samples = sz * 8 / (strdev->hwchannels * strdev->wire_bps);
    uint64_t timestamp = (oob_ptr) ? oob[0] : ~0ul;
    unsigned lbursts = (oob_ptr && (oob_size >= 2 * sizeof(uint64_t))) ? oob[1] : 0;
    if (lbursts > 3)
        return -EINVAL;
    if (samples > strdev->pkt_symbs / strdev->burst_count)
        return -EINVAL;

    return sfe_extx4_push_ring_buffer(strdev->base.dev->dev, 0, strdev->cnf_base,
                                      &strdev->cstx4.reg_0,
                                      &strdev->cstx4.reg_1,
                                      &strdev->cstx4.reg_2,
                                      sz, samples, lbursts, timestamp);
}

static int initialize_stream_tx_32(device_t* device,
                                   unsigned chcount,
                                   channel_info_t *channels,
                                   unsigned pktsyms,
                                   sfe_cfg_t* fecfg,
                                   unsigned sx_base,
                                   unsigned sx_sync,
                                   unsigned sx_base_rb,
                                   struct parsed_data_format pfmt,
                                   stream_sfetrx_dma32_t** outu,
                                   bool need_fd,
                                   bool data_lane_bifurcation,
                                   bool dont_check_fw)
{
    int res;
    stream_sfetrx_dma32_t* strdev;

    struct stream_config sc;
    unsigned logicchs = chcount;

    sc.burstspblk = 0;
    sc.chcnt = chcount;
    sc.channels = *channels;
    sc.sfmt = (pfmt.wire_fmt == NULL) ? pfmt.host_fmt : pfmt.wire_fmt;
    sc.spburst = pktsyms;

    if (logicchs > fecfg->cfg_raw_chans)
        return -EINVAL;

    if (!dont_check_fw) {
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
    }

    unsigned hardware_channels = logicchs;
    if (*sc.sfmt == '&') {
        logicchs = 1;
        sc.sfmt++;
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

    struct bitsfmt bfmt = get_bits_fmt(sc.sfmt);
    if (data_lane_bifurcation) {
        // TODO: proper lane bifurcation

        struct bitsfmt bfmt = get_bits_fmt(sc.sfmt);
        if ((bfmt.complex) && (sc.chcnt == 1)) {
            sc.chcnt = 2;
        } else {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Bifurcation is only valid for single channel complex");
            return -EINVAL;
        }
    }

    unsigned bits_per_single_sym = bfmt.bits * (bfmt.complex ? 2 : 1);

    // Check core fe sanity
    unsigned fe_old_tx_swap = 0;
    unsigned fe_old_tx_mute = 0;
    if (fecfg->cfg_fecore_id == CORE_SFETX_DMA32_R0) {
        if (!bfmt.complex || bfmt.bits != 16) {
            USDR_LOG("DSTR", USDR_LOG_ERROR, "Only 16 bit complex signals supported in Simple TX FE!\n");
            return -EINVAL;
        }

        if (sc.chcnt > 2)
            return -EINVAL;

        res = fe_tx4_swap_ab_get(sc.chcnt, &sc.channels, &fe_old_tx_swap);
        if (res) {
            return res;
        }

        fe_old_tx_mute = (sc.chcnt == 1) ? (fe_old_tx_swap ? 1 : 2) : 0;
    } else {
        if (sc.chcnt > (fecfg->cfg_raw_chans / (bfmt.complex ? 2 : 1)))
            return -EINVAL;

        unsigned llcanhs = (bfmt.complex ? 2 : 1) * sc.chcnt;
        unsigned expand;
        switch (llcanhs) {
        case 1: expand = 0; break;
        case 2: expand = 1; break;
        case 4: expand = 2; break;
        case 8: expand = 3; break;
        case 16: expand = 4; break;
        case 32: expand = 5; break;
        default:
            return -EINVAL;
        }

        res = res ? res : exfe_tx4_mute(fecfg, 0);
        res = res ? res : exfe_tx4_config(fecfg, bfmt.bits, expand);
        res = res ? res : exfe_trx4_update_chmap(fecfg, true, bfmt.complex, llcanhs, &sc.channels);

        if (res) {
            return res;
        }
    }

    res = sfetrx4_stream_sync(device, NULL, 0, "off");
    if (res)
        return res;

    lowlevel_stream_params_t sparams;
    stream_t sid;
    lowlevel_ops_t* dops = lowlevel_get_ops(device->dev);
    unsigned max_mtu = sfe_tx4_mtu_get(&sc);

    strdev = (stream_sfetrx_dma32_t*)malloc(sizeof(stream_sfetrx_dma32_t));
    if (!strdev)
        return -ENOMEM;

    sparams.streamno = 1;
    sparams.flags = 1;
    sparams.block_size = pktsyms * hardware_channels * bits_per_single_sym / 8;
    sparams.buffer_count = 32;
    sparams.flags = (need_fd) ? LLSF_NEED_FDPOLL : 0;
    sparams.channels = hardware_channels;
    sparams.bits_per_sym = hardware_channels * bits_per_single_sym;

    sparams.dma_core_id = fecfg->cfg_fecore_id;
    sparams.param = strdev;
    sparams.soft_tx_commit = (fecfg->cfg_fecore_id == CORE_SFETX_DMA32_R0) ? &stream_soft_tx_commit : &stream_soft_extx_commit;

    sparams.out_max_bursts = 1;
    if (sparams.block_size > max_mtu) {
        if (fecfg->cfg_fecore_id == CORE_EXFETX_DMA32_R0) {
            unsigned max_burst_sps = 8 * max_mtu / sparams.bits_per_sym;
            unsigned lgbrst;
            res = _extx_burstup(pktsyms, max_burst_sps, &lgbrst);
            if (res) {
                USDR_LOG("DSTR", USDR_LOG_CRITICAL_WARNING, "TX Stream couldn't breakup %d bytes in bursts, maximum per burst is %d!\n",
                         sparams.block_size, max_mtu);
                goto fail_dealloc;
            }

            sparams.out_max_bursts = (1 << lgbrst);
        } else {
            USDR_LOG("DSTR", USDR_LOG_CRITICAL_WARNING, "TX Stream maximum MTU is %d bytes, we need %d to deliver %d samples blocksize!\n",
                     max_mtu, sparams.block_size, pktsyms);
            res = -EINVAL;
            goto fail_dealloc;
        }
    }

    res = dops->stream_initialize(device->dev, 0, &sparams, &sid);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_CRITICAL_WARNING, "TX Stream couldn't initialize for %d block size\n", sparams.block_size);
        goto fail_dealloc;
    }

    if (pktsyms == 0) {
        if (sparams.out_mtu_size > max_mtu) {
            sparams.out_max_bursts = sparams.out_mtu_size / max_mtu;
            sparams.out_mtu_size = max_mtu;
        }

        pktsyms = 8 * sparams.out_mtu_size / (hardware_channels * bits_per_single_sym);
        USDR_LOG("DSTR", USDR_LOG_INFO, "TX Stream: No desired packetsize were set, assuminng maximum %d x %d sps (%d blocksize per burst)\n",
                 pktsyms, sparams.out_max_bursts, (unsigned)sparams.out_mtu_size);

        sparams.block_size = sparams.out_mtu_size;
    }

    strdev->base.dev = device;
    strdev->base.ops = &s_sfetr4_dma32_ops;

    strdev->type = USDR_ZCPY_TX;
    strdev->flags = 0;
    strdev->channels = logicchs;
    strdev->hwchannels = hardware_channels;
    strdev->ll_streamo = sid;

    strdev->cnf_base = sx_base;
    strdev->sync_base = sx_sync;
    strdev->cnfrd_base = sx_base_rb;

    strdev->pkt_symbs = pktsyms;
    strdev->pkt_bytes = sparams.block_size;
    strdev->host_bytes = funcs.sfunc(sparams.block_size, true);

    strdev->wire_bps = bits_per_single_sym; // 16bit I/Q

    strdev->tf_data = funcs.cfunc;
    strdev->tf_size = funcs.sfunc;

    strdev->cached_samples = ~0u;
    strdev->rcnt = 0;
    strdev->r_ts = 0; // Start timestamp

    strdev->stats.wirebytes = 0;
    strdev->stats.symbols = 0;
    strdev->stats.pktok = 0;
    strdev->stats.fe_drop = 0;
    strdev->stats.dma_drop = 0;

    strdev->fd = sparams.underlying_fd;

    strdev->burst_mask = 0;
    strdev->burst_count = sparams.out_max_bursts;
    strdev->burst_align_bytes = (fecfg->cfg_fecore_id == CORE_EXFETX_DMA32_R0) ? 16 : 1;

    strdev->fe_old_tx_mute = fe_old_tx_mute;
    strdev->fe_old_tx_swap = fe_old_tx_swap;
    strdev->fe_chans = sc.chcnt;
    strdev->fe_complex = bfmt.complex;
    strdev->storage.srx4 = *fecfg;
    extxcfg_cache_init(&strdev->cstx4);

    USDR_LOG("DSTR", USDR_LOG_INFO, "TX: Samples=%d Bps=%d WireBytes=%d HostBytes=%d Bursts=%d\n",
             strdev->pkt_symbs, strdev->wire_bps, strdev->pkt_bytes, strdev->host_bytes, strdev->burst_count);
    *outu = strdev;
    return 0;

fail_dealloc:
    free(strdev);
    return res;
}


int create_sfetrx4_stream(device_t* device,
                          unsigned core_id,
                          const char* dformat,
                          unsigned chcount,
                          channel_info_t *channels,
                          unsigned pktsyms,
                          unsigned flags,
                          unsigned sx_base,
                          unsigned sx_cfg_base,
                          unsigned sx_base_rb,
                          unsigned fe_fifobsz,
                          unsigned fe_base,
                          stream_handle_t** outu,
                          unsigned *hw_chans_cnt)
{
    bool need_fd = (flags & DMS_FLAG_NEED_FD) == DMS_FLAG_NEED_FD;
    bool bifurcation = (flags & DMS_FLAG_BIFURCATION) == DMS_FLAG_BIFURCATION;
    bool dontcheck = (flags & DMS_DONT_CHECK_FWID) == DMS_DONT_CHECK_FWID;
    char dfmt[256];
    int res;

    sfe_cfg_t fecfg;
    fecfg.dev = device->dev;
    fecfg.subdev = 0;
    fecfg.cfg_fecore_id = core_id;
    fecfg.cfg_base = fe_base;
    fecfg.cfg_fifomaxbytes = fe_fifobsz;

    strncpy(dfmt, dformat, sizeof(dfmt));
    struct parsed_data_format pfmt;
    if (stream_parse_dformat(dfmt, &pfmt)) {
        return -EINVAL;
    }

    switch (core_id) {
    case CORE_SFERX_DMA32_R0:
    case CORE_EXFERX_DMA32_R0:
        // TODO obtain dynamic config
        fecfg.cfg_word_bytes = (core_id == CORE_SFERX_DMA32_R0) ? 8 : 16;
        fecfg.cfg_raw_chans = (core_id == CORE_SFERX_DMA32_R0) ? 4 : 8;
        fecfg.cfg_dma_align_bytes = fecfg.cfg_word_bytes;

        res = initialize_stream_rx_32(device, chcount, channels, pktsyms,
                                      &fecfg, sx_base, sx_cfg_base, pfmt,
                                      (stream_sfetrx_dma32_t** )outu,
                                      need_fd, bifurcation);
        break;
    case CORE_SFETX_DMA32_R0:
    case CORE_EXFETX_DMA32_R0:
        // TODO obtain dynamic config
        fecfg.cfg_word_bytes = (core_id == CORE_SFETX_DMA32_R0) ? 8 : 16;
        fecfg.cfg_raw_chans = (core_id == CORE_SFETX_DMA32_R0) ? 4 : 8;
        fecfg.cfg_dma_align_bytes = fecfg.cfg_word_bytes;

        res = initialize_stream_tx_32(device, chcount, channels, pktsyms,
                                       &fecfg, sx_base, sx_cfg_base, sx_base_rb, pfmt,
                                       (stream_sfetrx_dma32_t** )outu,
                                       need_fd, bifurcation, dontcheck);
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
    int retimer_base;
    uint64_t sync_base;
    stream_sfetrx_dma32_t** pstream = (stream_sfetrx_dma32_t**)pstr;
    res = usdr_device_vfs_obj_val_get_u64(device, "/ll/sync/0/base", &sync_base);
    if (res) {
        USDR_LOG("DSTR", USDR_LOG_ERROR, "SYNC: Broken device! Coulnd't obtain sync addr: %d\n", res);
        return res;
    }
    retimer_base = sync_base;

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

    USDR_LOG("DSTR", USDR_LOG_NOTE, "SYNC[%x] Streams %d: %s => %d\n", retimer_base, scount, synctype, res);
    return res;
}




