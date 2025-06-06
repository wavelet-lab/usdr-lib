// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>

#include "device.h"
#include "device_vfs.h"

#include "../ipblks/streams/streams_api.h"

#include "mdev.h"

static int _mdev_get_obj(pdevice_t dev, const char* fullpath, pusdr_vfs_obj_t *obj);
static int _mdev_create_stream(device_t* dev, const char* sid, const char* dformat,
                               const usdr_channel_info_t* channels, unsigned pktsyms,
                               unsigned flags, const char* parameters, stream_handle_t** out_handle);
static int _mdev_unregister_stream(device_t* dev, stream_handle_t* stream);
static void _mdev_destroy(device_t *udev);

static int _mdev_stream_sync(device_t* device,
                             stream_handle_t** pstr, unsigned scount, const char* synctype);

#define DEV_MAX 32
#define STREAMS_MAX 2

struct stream_mdev {
    stream_handle_t base;

    bool dev_mask[DEV_MAX];
    struct pollfd poll_fd[DEV_MAX];
    unsigned dev_idx[DEV_MAX];

    unsigned dev_cnt;

    // Info
    unsigned type;
    unsigned channels;
    unsigned pkt_bytes;
    unsigned pkt_symbs;

    // Stat
};
typedef struct stream_mdev stream_mdev_t;

struct dev_multi {
    // Virtual lowlevel
    lowlevel_dev_t lldev;

    // Virtual stream function
    device_t virt_dev;
    stream_mdev_t streams[STREAMS_MAX];
    unsigned streams_used;

    // Physical devices
    unsigned cnt;
    lldev_t real[DEV_MAX];
    //struct pollfd poll_fd[DEV_MAX];

    unsigned rx_chans;
    unsigned tx_chans;

    stream_handle_t* real_str_rx[DEV_MAX];
    stream_handle_t* real_str_tx[DEV_MAX];

    // FIXUP! Remove me after fixing vfs operations
    vfs_object_t vfs_obj;
};
typedef struct dev_multi dev_multi_t;



static
int mdev_generic_get(lldev_t dev, int generic_op, const char** pout)
{
    //pcie_uram_dev_t* d = (pcie_uram_dev_t*)dev;
    dev_multi_t* obj =  container_of(dev, dev_multi_t, lldev);

    switch (generic_op) {
    case LLGO_DEVICE_NAME: *pout = "Multidev"; return 0;
    case LLGO_DEVICE_UUID: return obj->real[0]->ops->generic_get(obj->real[0], generic_op, pout);
    }

    return -EINVAL;
}

static
int mdev_generic_destroy(lldev_t dev)
{
    dev_multi_t* obj =  container_of(dev, dev_multi_t, lldev);

    // Causes double free
    //for (unsigned i = 0; i < STREAMS_MAX; i++) {
    //    _mdev_unregister_stream(&obj->virt_dev, &obj->streams[i].base);
    //}

    // Destroy underlying lldevs
    for (unsigned i = 0; i < obj->cnt; i++) {
        if (obj->real[i] == NULL) {
            USDR_LOG("MDEV", USDR_LOG_WARNING, "Uderlying device has been destroyed already!\n");
            continue;
        }

        obj->real[i]->ops->destroy(obj->real[i]);
        obj->real[i] = NULL;
    }

    // Destroy undelying dev
    if (dev->pdev) {
        dev->pdev->destroy(dev->pdev);
    }

    USDR_LOG("MDEV", USDR_LOG_INFO, "MDevice has beed destroyed!\n");

    free(obj);
    return 0;
}

static
struct lowlevel_ops s_mdev_ops = {
    mdev_generic_get,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    mdev_generic_destroy,
};



static int _mdev_obj_set_i64(pusdr_vfs_obj_t vfsobj, uint64_t value)
{
    dev_multi_t* obj = (dev_multi_t*)vfsobj->object;
    int res;

    for (unsigned i = 0; i < obj->cnt; i++) {
        pdevice_t child_dev = obj->real[i]->pdev;
        res = usdr_device_vfs_obj_val_set_by_path(child_dev, vfsobj->full_path, value);

        if (res) {
            return res;
        }
    }

    USDR_LOG("DSTR", USDR_LOG_TRACE, "MDEV VFS %s set to %lld\n",
             vfsobj->full_path, (long long)value);
    return 0;
}

static int _mdev_obj_get_i64(pusdr_vfs_obj_t vfsobj, uint64_t* ovalue)
{
    dev_multi_t* obj = (dev_multi_t*)vfsobj->object;
    pdevice_t child_dev = obj->real[0]->pdev;
    int res;

    if (strcmp(vfsobj->full_path, "/ll/devices") == 0) {
        *ovalue = obj->cnt;
        return 0;
    }

    res = usdr_device_vfs_obj_val_get_u64(child_dev, vfsobj->full_path, ovalue);
    if (res) {
        return res;
    }

    return res;
}


int _mdev_get_obj(pdevice_t dev, const char* fullpath, pusdr_vfs_obj_t *vfsobj)
{
    dev_multi_t* obj = container_of(dev, dev_multi_t, virt_dev);
    vfs_object_t *vfso = &obj->vfs_obj;

    vfso->type = VFST_I64;
    vfso->amask = 0;
    vfso->eparam[0] = 0;
    vfso->eparam[1] = 0;
    vfso->eparam[2] = 0;
    vfso->object = obj;
    vfso->ops.si64 = &_mdev_obj_set_i64;
    vfso->ops.gi64 = &_mdev_obj_get_i64;
    vfso->ops.sstr = NULL;
    vfso->ops.gstr = NULL;
    vfso->ops.sai64 = NULL;
    vfso->ops.gai64 = NULL;
    vfso->data.i64 = 0;
    strncpy(vfso->full_path, fullpath, sizeof(vfso->full_path));

    *vfsobj = vfso;
    return 0;
}

static
int _mstr_stream_destroy(stream_handle_t* stream)
{
    stream_mdev_t* str = container_of(stream, stream_mdev_t, base);
    dev_multi_t* obj =  container_of(stream->dev, dev_multi_t, virt_dev);
    stream_handle_t** real_str = str->type == USDR_DMS_RX ? obj->real_str_rx : obj->real_str_tx;

    for (unsigned i = 0; i < obj->cnt; i++) {
        if (!str->dev_mask[i]) {
            USDR_LOG("MDEV", USDR_LOG_TRACE, "Device %d ignored\n", i);
            continue;
        }

        real_str[i]->ops->destroy(real_str[i]);

        // Mark as unused
        real_str[i] = NULL;
        str->dev_mask[i] = false;
    }

    return 0;
}

static
int _mstr_stream_op(stream_handle_t* stream, unsigned command, dm_time_t timestamp)
{
    stream_mdev_t* str = container_of(stream, stream_mdev_t, base);
    dev_multi_t* obj =  container_of(stream->dev, dev_multi_t, virt_dev);
    stream_handle_t** real_str = str->type == USDR_DMS_RX ? obj->real_str_rx : obj->real_str_tx;

    int res;

    for (unsigned i = 0; i < obj->cnt; i++) {
        if (!str->dev_mask[i])
            continue;

        res = real_str[i]->ops->op(real_str[i], command, timestamp);
        if (res)
            return res;
    }

    USDR_LOG("MDEV", USDR_LOG_TRACE, "Stream operation: %d@%lld\n",
             command, (long long)timestamp);
    return 0;
}


static
int _mstr_stream_recv(stream_handle_t* stream,
                      char **stream_buffs,
                      unsigned timeout,
                      struct usdr_dms_recv_nfo* nfo)
{
    stream_mdev_t* str = container_of(stream, stream_mdev_t, base);
    dev_multi_t* obj =  container_of(stream->dev, dev_multi_t, virt_dev);
    stream_handle_t** real_str = str->type == USDR_DMS_RX ? obj->real_str_rx : obj->real_str_tx;
    size_t step = str->channels / str->dev_cnt;

    struct usdr_dms_recv_nfo lnfo[DEV_MAX];

    int res, i, idx;
    for (i = 0; i < str->dev_cnt; i++) {
        idx = str->dev_idx[i];

        res = real_str[idx]->ops->recv(real_str[idx], stream_buffs + step * i,
                                       timeout, &lnfo[i]);
        if (res)
            return res;
    }

    // TODO aggregate
    if (nfo)
        *nfo = lnfo[0];
    return 0;
}

static
int _mstr_stream_send(stream_handle_t* stream,
                      const char **stream_buffs,
                      unsigned samples,
                      dm_time_t timestamp,
                      unsigned timeout_ms,
                      usdr_dms_send_stat_t* stat)
{
    stream_mdev_t* str = container_of(stream, stream_mdev_t, base);
    dev_multi_t* obj =  container_of(stream->dev, dev_multi_t, virt_dev);
    stream_handle_t** real_str = str->type == USDR_DMS_RX ? obj->real_str_rx : obj->real_str_tx;
    size_t step = str->channels / str->dev_cnt;

    struct usdr_dms_send_stat lstat[DEV_MAX];

    int res, i, idx;
    for (i = 0; i < str->dev_cnt; i++) {
        idx = str->dev_idx[i];

        res = real_str[idx]->ops->send(real_str[idx], stream_buffs + step * i,
                                       samples, timestamp, timeout_ms, &lstat[i]);
        if (res)
            return res;
    }

    // TODO aggregate
    if (stat)
        *stat = lstat[0];

    return 0;
}

static
int _mstr_stream_stat(stream_handle_t* stream, usdr_dms_nfo_t* nfo)
{
    stream_mdev_t* obj =  container_of(stream, stream_mdev_t, base);

    nfo->type = obj->type;
    nfo->channels = obj->channels;
    nfo->pktbszie = obj->pkt_bytes;
    nfo->pktsyms = obj->pkt_symbs;

    return 0;
}

// Custom stream options
static
int _mstr_stream_option_get(stream_handle_t* stream, const char* name, int64_t* out_val)
{
    return -EINVAL;
}

static
int _mstr_stream_option_set(stream_handle_t* stream, const char* name, int64_t in_val)
{
    return -EINVAL;
}

static const
stream_ops_t _mstr_ops = {
    .destroy = &_mstr_stream_destroy,
    .op = &_mstr_stream_op,
    .recv = &_mstr_stream_recv,
    .send = &_mstr_stream_send,
    .stat = &_mstr_stream_stat,
    .option_get = &_mstr_stream_option_get,
    .option_set = &_mstr_stream_option_set,
};


static
int _mdev_create_stream(device_t* dev, const char* sid, const char* dformat,
                        const usdr_channel_info_t* channels, unsigned pktsyms,
                        unsigned flags, const char* parameters, stream_handle_t** out_handle)
{
    dev_multi_t* obj =  container_of(dev, dev_multi_t, virt_dev);
    bool rx = (strstr(sid, "rx/0") == 0) ? true : false;
    bool tx = (strstr(sid, "tx/0") == 0) ? true : false;
    if (!rx && !tx) {
        USDR_LOG("MDEV", USDR_LOG_TRACE, "Unknown stream name `%s`\n", sid);
        return -EINVAL;
    }
    unsigned idx = (rx) ? 0 :  1;
    stream_mdev_t *mstr = &obj->streams[idx];
    int64_t tmp;
    usdr_dms_nfo_t nfo;
    stream_handle_t** real_str = rx ? obj->real_str_rx : obj->real_str_tx;
    unsigned chans_per_dev = (rx) ? obj->rx_chans : obj->tx_chans;
    int res;

    // Bifurcate to children
    unsigned pcnt = 0;
    for (unsigned i = 0; i < obj->cnt; i++) {
        usdr_channel_info_t subdev_info;
        unsigned phys_nums[64];
        const char* phys_names[64];

        subdev_info.count = channels->count / obj->cnt;
        subdev_info.flags = channels->flags;
        subdev_info.phys_names = channels->phys_names ? phys_names : NULL;
        subdev_info.phys_nums = channels->phys_nums ? phys_nums : NULL;

        // TODO proper parse with specific chnnel mixing
        for (unsigned k = 0; k < chans_per_dev; k++) {
            if (channels->phys_names) {
                phys_names[k] = channels->phys_names[k];
            }
            if (channels->phys_nums) {
                phys_nums[k] = channels->phys_nums[k];
            }
        }

        mstr->dev_mask[i] = false;
        /*
        if (child_msk == 0) {
            USDR_LOG("MDEV", USDR_LOG_TRACE, "Device %d ignored\n", i);
            continue;
        }
        */

        if (real_str[i]) {
            USDR_LOG("MDEV", USDR_LOG_WARNING, "Device %d stream is already in use!\n", i);
            return -EBUSY;
        }

        USDR_LOG("MDEV", USDR_LOG_INFO, "Creating stream for dev %d with %d channels\n", i, chans_per_dev);
        pdevice_t child_dev = obj->real[i]->pdev;
        res = child_dev->create_stream(child_dev, sid, dformat, &subdev_info, pktsyms, flags, parameters,
                                       &real_str[i]);
        if (res) {
            USDR_LOG("MDEV", USDR_LOG_ERROR, "Failed to create strem for dev %d: FMT %s, syms %d SI={CNT=%d FLAGS=%d}: Error %d\n",
                     i, dformat, pktsyms, subdev_info.count, subdev_info.flags, res);
            return res;
        }

        mstr->dev_mask[i] = true;

        tmp = -1;
        res = real_str[i]->ops->option_get(real_str[i], "fd", &tmp);
        if (res)
            return res;

        if (tmp < 0) {
            USDR_LOG("MDEV", USDR_LOG_ERROR, "Need a real in LL layer to work in MDEV configuration for %s\n",
                     sid);
            return -ENOTSUP;
        }

        mstr->poll_fd[pcnt].fd = tmp;
        mstr->poll_fd[pcnt].events = (rx) ? POLLIN : POLLOUT;
        mstr->dev_idx[pcnt] = i;

        USDR_LOG("MDEV", USDR_LOG_TRACE, "Device %d added fd %d\n", i, (int)tmp);
        pcnt++;
    }

    res = real_str[0]->ops->stat(real_str[0], &nfo);
    if (res)
        return res;

    USDR_LOG("DSTR", USDR_LOG_TRACE, "Natural format %d x (%d chans %d bytes %d symbs)\n",
             pcnt, nfo.channels, nfo.pktbszie, nfo.pktsyms);

    mstr->type = rx ? USDR_DMS_RX : USDR_DMS_TX;
    mstr->channels = pcnt * nfo.channels;
    mstr->pkt_bytes = nfo.pktbszie;
    mstr->pkt_symbs = nfo.pktsyms;

    mstr->base.dev = dev;
    mstr->base.ops = &_mstr_ops;
    mstr->dev_cnt = pcnt;

    *out_handle = (stream_handle_t*)mstr;
    return 0;

// TODO error handling

}

int _mdev_stream_sync(device_t* dev,
                      stream_handle_t** pstr, unsigned scount, const char* synctype)
{
    dev_multi_t* obj =  container_of(dev, dev_multi_t, virt_dev);
    bool sysref = !strcmp(synctype, "sysref");
    bool sysref_gen = !strcmp(synctype, "sysref+gen");
    int res;
    stream_mdev_t* mstr[2] = {
        (pstr[0]) ? container_of(pstr[0], stream_mdev_t, base) : NULL,
        (scount > 1 && pstr[1]) ? container_of(pstr[1], stream_mdev_t, base) : NULL,
    };

    if (sysref_gen) {
        return -EINVAL;
    }
    if (scount > 2 || scount == 0) {
        return -EINVAL;
    }

    for (unsigned i = 0; i < obj->cnt; i++) {
        stream_handle_t* strs[2] = {
            mstr[0] ? (mstr[0]->type == USDR_DMS_RX ? obj->real_str_rx[i] : obj->real_str_tx[i]) : NULL,
            mstr[1] ? (mstr[1]->type == USDR_DMS_RX ? obj->real_str_rx[i] : obj->real_str_tx[i]) : NULL,
        };

        // TODO check this
        if (mstr[0] && !mstr[0]->dev_mask[i])
            continue;
        if (mstr[1] && !mstr[1]->dev_mask[i])
            continue;

        res = obj->real[i]->pdev->timer_op(obj->real[i]->pdev, strs, scount, synctype);
        if (res)
            return res;
    }

    // Start SYSREF generator on master
    if (sysref) {
        res = obj->real[0]->pdev->timer_op(obj->real[0]->pdev, NULL, 0, "sysref+gen");
        if (res)
            return res;
    }

    USDR_LOG("MDEV", USDR_LOG_WARNING, "Sync operation cnt:%d `%s`\n",
             scount, synctype);
    return 0;
}


static
int _mdev_unregister_stream(device_t* dev, stream_handle_t* stream)
{
    USDR_LOG("MDEV", USDR_LOG_INFO, "MDevice unregister stream\n");

    stream_mdev_t* str = container_of(stream, stream_mdev_t, base);
    dev_multi_t* obj =  container_of(stream->dev, dev_multi_t, virt_dev);
    stream_handle_t** real_str = str->type == USDR_DMS_RX ? obj->real_str_rx : obj->real_str_tx;

    int i, idx;
    for (i = 0; i < str->dev_cnt; i++) {
        pdevice_t child_dev = obj->real[i]->pdev;
        idx = str->dev_idx[i];

        child_dev->unregister_stream(child_dev, real_str[idx]);
        real_str[idx] = NULL;
    }

    return 0;
}

static
void _mdev_destroy(device_t *udev)
{
    USDR_LOG("MDEV", USDR_LOG_INFO, "MDevice destroy\n");
    usdr_device_base_destroy(udev);
}


int mdev_create(unsigned pcnt, const char** names, const char** values, lldev_t* odev,
                unsigned idx, char** bus_names, unsigned bus_cnt)
{
    dev_multi_t* obj;
    int res;
    unsigned i;
    const uint8_t* uuid_master;

    if (bus_cnt == 0 || bus_cnt > DEV_MAX) {
        return -EINVAL;
    }

    obj = (dev_multi_t*)malloc(sizeof(dev_multi_t));
    if (obj == NULL) {
        return -ENOMEM;
    }
    memset(obj, 0, sizeof(*obj));

    // Creating sub-device
    for (i = 0; i < bus_cnt; i++) {
        values[idx] = bus_names[i];

        USDR_LOG("DSTR", USDR_LOG_WARNING, "Creating %d: '%s' \n",
                 i, bus_names[i]);

        res = lowlevel_create(pcnt, names, values, &obj->real[i], 0, NULL, 0);
        if (res)
            goto failed_create;

        if (i == 0) {
            uuid_master = lowlevel_get_uuid(obj->real[i]);
        } else {
            const uint8_t* uuid = lowlevel_get_uuid(obj->real[i]);
            if (memcmp(uuid_master, uuid, 16) != 0) {
                USDR_LOG("DSTR", USDR_LOG_WARNING, "Device %d isn't compatible with master!\n", i);
                goto error_init;
            }
        }
    }

    // Get channel configuration
    res = res ? res : usdr_device_vfs_obj_val_get_u32(obj->real[0]->pdev, "/ll/sdr/max_hw_rx_chans", &obj->rx_chans);
    res = res ? res : usdr_device_vfs_obj_val_get_u32(obj->real[0]->pdev, "/ll/sdr/max_hw_tx_chans", &obj->tx_chans);

    obj->cnt = bus_cnt;

    // Create virtual lowlevel device
    obj->lldev.ops = &s_mdev_ops;

    // Create virtual device
    res = res ? res : usdr_device_base_create(&obj->virt_dev, &obj->lldev);
    if (res) {
        goto error_init;
    }

    obj->lldev.pdev = &obj->virt_dev;
    obj->virt_dev.destroy = &_mdev_destroy;
    obj->virt_dev.create_stream = &_mdev_create_stream;
    obj->virt_dev.unregister_stream = &_mdev_unregister_stream;
    obj->virt_dev.timer_op = &_mdev_stream_sync;
    obj->virt_dev.vfs_get_single_object = &_mdev_get_obj;

    // Set multi dev for master node
    res = res ? res : usdr_device_vfs_obj_val_set_by_path(obj->real[0]->pdev, "/ll/mdev", (uintptr_t)&obj->lldev);
    if (res) {
        goto error_init;
    }

    USDR_LOG("DSTR", USDR_LOG_WARNING, "Creating multi device with %d nodes, each %d/%d RX/TX chans, MDEV captured: %d\n",
             obj->cnt, obj->rx_chans, obj->tx_chans, res == 0 ? 1 : 0);
    *odev = &obj->lldev;
    return 0;

error_init:
failed_create:
    for (i = 0; i < bus_cnt; i++) {
        if (obj->real[i]) {
            obj->real[i]->ops->destroy(obj->real[i]);
            obj->real[i] = NULL;
        }
    }
    free(obj);
    return res;
}
