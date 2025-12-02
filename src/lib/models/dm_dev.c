// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dm_dev_impl.h"
#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>

#include "../device/device.h"
#include "../device/device_vfs.h"
#include "../device/mdev.h"

#include "dm_debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>

#include "dm_stream.h"
#include "../ipblks/streams/streams_api.h"

#include "../xdsp/vbase.h"

#define DEV_MAX 32


#define MAX_PARAMS 64
struct dev_params {
    char* params[MAX_PARAMS];
    char* value[MAX_PARAMS];
    unsigned num;
    char lconnstr[2048];
};
typedef struct dev_params dev_params_t;

static void _usdr_dmd_parse_params(const char* connection_string, struct dev_params *pd)
{
    unsigned number = 0;
    char* str;
    char* saveptr;

    if (connection_string) {
        strncpy(pd->lconnstr, connection_string, sizeof(pd->lconnstr) - 1);
        pd->lconnstr[2047] = 0;
    } else {
        pd->lconnstr[0] = 0;
    }

    for (str = pd->lconnstr; number < MAX_PARAMS; str = NULL, number++) {
        char* token = strtok_r(str, ",", &saveptr);
        if (token == NULL)
            break;

        char* par = strchr(token, '=');
        char* val = NULL;

        if (par) {
            *par = 0;
            val = par + 1;

            if (*val == 0)
                val = NULL;
        }

        pd->params[number] = token;
        pd->value[number] = val;

        USDR_LOG("DSTR", USDR_LOG_TRACE, " dev param list %d: `%s`=`%s`\n",
                 number, pd->params[number], pd->value[number]);
    }

    pd->num = number;
}

int usdr_dmd_discovery(const char* filer_string, unsigned max_buf, char* devlist)
{
    char discoverd[4096];
    struct dev_params par;
    _usdr_dmd_parse_params(filer_string, &par);

    int res = lowlevel_discovery(par.num, (const char**)par.params, (const char**)par.value,
                                 sizeof(discoverd), discoverd);
    if (res == 0) {
        *devlist = 0;
        return -ENODEV;
    }

    strncpy(devlist, discoverd, max_buf);
    return res;
}

static
int _usdr_dmd_create(const struct dev_params *par, pdm_dev_t* odev,
                     unsigned idx, char** bus_names, unsigned bus_cnt,
                     unsigned vidpid, void* webops, uintptr_t param)
{
    int res;
    lldev_t lldev = NULL;
    pdm_dev_t dev;

    if (bus_cnt <= 1) {
        res = lowlevel_create(par->num, (const char**)par->params, (const char**)par->value, &lldev, vidpid, webops, param);
    } else {
        res = mdev_create(par->num, (const char**)par->params, (const char**)par->value, &lldev,
                          idx, bus_names, bus_cnt);
    }
    if (res)
        return res;

    dev = (pdm_dev_t)malloc(sizeof(dm_dev_t));
    if (dev == NULL)
        return -ENOMEM;

    dev->lldev = lldev;
    dev->debug_obj = NULL;

#ifndef __EMSCRIPTEN__
    if (getenv("USDR_DEBUG")) {
        USDR_LOG("DSTR", USDR_LOG_WARNING, "Configured with DEBUG interface!\n");
        res = usdr_dif_init(NULL, dev, &dev->debug_obj);
        if (res) {
            USDR_LOG("DSTR", USDR_LOG_WARNING, "Failed to run debug service on socket! erorr=%d\n", res);
            return res;
        }
    }
#endif

    usdr_dmo_init(&dev->obj_head, NULL);

    *odev = dev;
    return 0;
}

int usdr_dmd_create_webusb(unsigned vidpid, void* webops, uintptr_t param, pdm_dev_t* odev)
{
    dev_params_t par;
    memset(&par, 0, sizeof(dev_params_t));

    char buffer[128];
    unsigned max_cpu = ~0u;

    cpu_vcap_str(buffer, sizeof(buffer), cpu_vcap_obtain(CVF_LIMIT_VCPU | max_cpu));
    USDR_LOG("DSTR", USDR_LOG_WARNING, "Running with %s CPU features\n", buffer);

    return _usdr_dmd_create(&par, odev, 0, NULL, 0, vidpid, webops, param);
}

int usdr_dmd_create_string(const char* connection_string, pdm_dev_t* odev)
{
    struct dev_params par;
    char buffer[128];
    unsigned max_cpu = ~0u;
    char* bus[DEV_MAX] = { NULL, };
    char bus_buffer[4096] = { 0 };
    unsigned bus_cnt = 0;
    unsigned bus_idx = 0;

    _usdr_dmd_parse_params(connection_string, &par);

    for (unsigned k = 0; k < par.num; k++) {
        if (strcmp(par.params[k], "cpulimit") == 0) {
            max_cpu = atoi(par.value[k]);
            USDR_LOG("DSTR", USDR_LOG_WARNING, "Limiting CPU feature to %d\n", max_cpu);
            break;
        } else if (strcmp(par.params[k], "bus") == 0) {
            bus_idx = k;
            snprintf(bus_buffer, sizeof(bus_buffer), "%s", par.value[k]);

            unsigned j;
            char *str, *token, *saveptr;
            for (j = 0, str = bus_buffer; j < DEV_MAX; j++, str = NULL) {
                token = strtok_r(str, ":", &saveptr);
                bus[j] = token;
                if (token == NULL)
                    break;

                USDR_LOG("DSTR", USDR_LOG_NOTE, "BUS[%d] = %s\n", j, token);
            }
            bus_cnt = j;
        } else if (strcmp(par.params[k], "loglevel") == 0) {
            int loglevel = atoi(par.value[k]);
            usdrlog_setlevel(NULL, loglevel);
        }
    }

    cpu_vcap_str(buffer, sizeof(buffer), cpu_vcap_obtain(CVF_LIMIT_VCPU | max_cpu));
    USDR_LOG("DSTR", USDR_LOG_WARNING, "Running with %s CPU features\n", buffer);

    return _usdr_dmd_create(&par, odev, bus_idx, bus, bus_cnt, 0, NULL, 0);
}

int usdr_dmd_close(pdm_dev_t dev)
{
    while (dev->obj_head.prev != &dev->obj_head) {
        USDR_LOG("DSTR", USDR_LOG_DEBUG, "Destroying object %p!\n", dev->obj_head.prev);
        usdr_dmo_destroy(dev->obj_head.prev);
    }

    if (dev->debug_obj) {
        usdr_dif_free(dev->debug_obj);
    }

    lowlevel_destroy(dev->lldev);
    free(dev);

    return 0;
}

int usdr_dme_findsetv_uint(pdm_dev_t dev, const char *directory, unsigned count, const struct dme_findsetv_data* pdata)
{
    char pname[4096];
    int res;

    for (unsigned i = 0; i < count; i++) {
        const struct dme_findsetv_data* pd = &pdata[i];

        if (pd->ignore)
            continue;

        snprintf(pname, sizeof(pname), "%s%s", directory == NULL ? "" : directory, pd->name);

        res = usdr_dme_set_uint(dev, pname, pd->value);
        if (res) {
            USDR_LOG("DSTR", pd->stopOnFail ? USDR_LOG_WARNING : USDR_LOG_NOTE, "Unable to set `%s` to %" PRIu64 " error: %d!\n", pname, pd->value, res);
            if (pd->stopOnFail)
                return res;
        }

        USDR_LOG("DSTR", USDR_LOG_INFO, "Set `%s` to %" PRIu64 "\n", pname, pd->value);
    }

    return 0;
}

int usdr_dme_get_u32(pdm_dev_t dev, const char* path, uint32_t *oval)
{
    uint64_t v;
    int res = usdr_dme_get_uint(dev, path, &v);
    if (res)
        return res;

    *oval = v;
    return 0;
}

int usdr_dme_get_uint(pdm_dev_t dev, const char* path, uint64_t *oval)
{
    pusdr_vfs_obj_t obj;
    pdevice_t udev = lowlevel_get_device(dev->lldev);
    int res = udev->vfs_get_single_object(udev, path, &obj);
    if (res)
        return res;

    return usdr_device_vfs_obj_val_get(udev, obj, oval);
}

int usdr_dme_get_string(pdm_dev_t dev, const char* path, const char** oval)
{
    uint64_t v;
    int res = usdr_dme_get_uint(dev, path, &v);
    if (res)
        return res;

    *oval = (const char*)(uintptr_t)v;
    return 0;
}

int usdr_dme_set_uint(pdm_dev_t dev, const char* path, uint64_t val)
{
    pusdr_vfs_obj_t obj;
    pdevice_t udev = lowlevel_get_device(dev->lldev);
    int res = udev->vfs_get_single_object(udev, path, &obj);
    if (res)
        return res;

    return usdr_device_vfs_obj_val_set(udev, obj, val);
}

int usdr_dme_set_string(pdm_dev_t dev, const char* path, const char* val)
{
    return usdr_dme_set_uint(dev, path, (uintptr_t)val);
}

int usdr_dme_filter(pdm_dev_t dev, const char* pattern, const unsigned count, dme_param_t* objs)
{
    vfs_filter_obj_t ostor[count];
    pdevice_t udev = lowlevel_get_device(dev->lldev);
    int res = udev->vfs_filter(udev, pattern, count, ostor);

    for (int i = 0; i < res; i++) {
        objs[i].fullpath = ostor[i].fullpath;
    }
    return res;
}

#if 0
struct notify_data {
    usdr_dm_obj_t base;
    pdm_dev_t dev;
    const usdr_vfs_obj_ops_t* chain_ops;
    pusdr_vfs_obj_t obj;
    union {
        entity_cb_notify_t func_notify;
        entity_cb_filer_t func_filt;
        void* func;
    };
    void* func_param;
    usdr_vfs_obj_ops_t ops;
    dm_dev_entity_t entity;
};
typedef struct notify_data notify_data_t;

static void notify_data_release(usdr_dm_obj_t* obj)
{
    notify_data_t* nd = (notify_data_t*)obj;
    *(const usdr_vfs_obj_ops_t**)nd->obj = nd->chain_ops;
    free(nd);
}
static const struct usdr_dm_obj_ops s_notify_data_ops = {
    notify_data_release
};

static
int notify_data_set_func(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    usdr_vfs_obj_ops_t* ops = usdr_vfs_obj_ops(obj);
    notify_data_t* nd = container_of(ops, notify_data_t, ops);
    int res = nd->chain_ops->val_set(ud, obj, value);
    if (res)
        return res;

    nd->func_notify(nd->dev, &nd->entity, nd->func_param, value);
    return 0;
}

static
int filter_data_set_func(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    usdr_vfs_obj_ops_t* ops = usdr_vfs_obj_ops(obj);
    notify_data_t* nd = container_of(ops, notify_data_t, ops);
    int res = nd->func_filt(nd->dev, &nd->entity, nd->func_param, value, &value);
    if (res)
        return res;

    return nd->chain_ops->val_set(ud, obj, value);
}

static int dme_filt_notify(pdm_dev_t dev, dm_dev_entity_t entity, void* param,
                           usdr_vfs_set_func_t sfunc, void* func)
{
    pusdr_vfs_obj_t obj;
    pdevice_t udev = lowlevel_get_device(dev->lldev);
    int res = usdr_device_vfs_get_by_uid(udev, entity.duid, &obj);
    if (res)
        return res;
    if (entity.impl)
        return -ENOTSUP;

    struct notify_data* nd = malloc(sizeof(struct notify_data));
    usdr_dmo_init(&nd->base, &s_notify_data_ops);
    nd->dev = dev;
    nd->chain_ops = usdr_vfs_obj_ops(obj);
    nd->obj = obj;
    nd->func = func;
    nd->func_param = param;
    nd->entity = entity;
    nd->ops.val_get = nd->chain_ops->val_get;
    nd->ops.val_set = sfunc;

    *(usdr_vfs_obj_ops_t**)obj = &nd->ops;
    usdr_dmo_append(&dev->obj_head, &nd->base);
    return 0;
}

int usdr_dme_notify(pdm_dev_t dev, dm_dev_entity_t entity, entity_cb_notify_t fn, void* param)
{
    return dme_filt_notify(dev, entity, param,
                           notify_data_set_func, fn);
}

int usdr_dme_filter(pdm_dev_t dev, dm_dev_entity_t entity, entity_cb_filer_t fn, void* param)
{
    return dme_filt_notify(dev, entity, param,
                           filter_data_set_func, fn);
}



void usdr_dme_release(pdm_dev_t dev, dm_dev_entity_t entity)
{
    // Nothing to do for now
}
#endif
