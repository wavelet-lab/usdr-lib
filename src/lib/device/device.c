// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "device.h"
#include <string.h>
#include <usdr_logging.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "device_vfs.h"
#include <fnmatch.h>

static int _usdr_device_vfs_get_by_path(device_t *base, const char* fullpath, pusdr_vfs_obj_t *obj);
int usdr_device_base_create(pdevice_t dev, lldev_t lldev)
{
    dev->dev = lldev;
    dev->initialize = NULL;
    dev->destroy = NULL;
    dev->create_stream = NULL;
    dev->unregister_stream = NULL;
    dev->timer_op = NULL;
    dev->vfs_get_single_object = &_usdr_device_vfs_get_by_path;
    dev->vfs_filter = &usdr_device_vfs_filter;
    return vfs_folder_init(&dev->rootfs, "", dev);
}

int usdr_device_base_destroy(pdevice_t dev)
{
    vfs_folder_destroy(&dev->rootfs);
    return 0;
}


struct device_dictionary {
    device_id_t devid;
    const struct device_factory_ops* ops;
};
enum {
    MAX_DEVS = 32,
};

static struct device_dictionary s_devdict[MAX_DEVS];
static unsigned s_devdict_count;

const char* usdr_device_id_to_str(device_id_t devid)
{
    static PORT_THREAD char uuid_str[40];

    snprintf(uuid_str, sizeof(uuid_str), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             devid.d[0], devid.d[1], devid.d[2], devid.d[3],
             devid.d[4], devid.d[5], devid.d[6], devid.d[7],
             devid.d[8], devid.d[9], devid.d[10], devid.d[11],
             devid.d[12], devid.d[13], devid.d[14], devid.d[15]);

    return uuid_str;
}

int usdr_device_register(device_id_t devid, const struct device_factory_ops* ops)
{
    if (s_devdict_count >= MAX_DEVS)
        return -EINVAL;

    struct device_dictionary *d = &s_devdict[s_devdict_count];
    d->devid = devid;
    d->ops = ops;

    s_devdict_count++;

    USDR_LOG("UDEV", USDR_LOG_INFO, "Registered device %s\n",
             usdr_device_id_to_str(devid));
    return 0;
}

int usdr_device_register_m2_lm6_1();
int usdr_device_register_m2_lm7_1();
int usdr_device_register_limesdr();
int usdr_device_register_m2_d09_4_ad45_2();
int usdr_device_register_m2_dsdr();

int usdr_device_init()
{
    s_devdict_count = 0;

    // Static Device initialization
    usdr_device_register_m2_lm6_1();
    usdr_device_register_m2_lm7_1();
    usdr_device_register_limesdr();
    usdr_device_register_m2_d09_4_ad45_2();
    usdr_device_register_m2_dsdr();


    // Dynamic Device initialization
    return 0;
}

int usdr_device_create(lldev_t dev, device_id_t devid)
{
    assert(dev->pdev == NULL);

    for (unsigned i = 0; i < s_devdict_count; i++) {
        const struct device_dictionary *d = &s_devdict[i];
        if (memcmp(&d->devid, &devid, sizeof(device_id_t)) == 0) {
            USDR_LOG("UDEV", USDR_LOG_TRACE, "Creating device '%s' for '%s'\n",
                     usdr_device_id_to_str(devid), lowlevel_get_devname(dev));
            return d->ops->create(dev, devid);
        }
    }

    return -ENOENT;
}

int usdr_device_destroy(pdevice_t udev)
{
   udev->destroy(udev);
   return 0;
}


void __attribute__ ((constructor(120))) setup_dev(void) {
    usdr_device_init();
}



// Device VFS operations
// TODO: Move away
int usdr_device_vfs_filter(pdevice_t dev, const char* filter, unsigned max_objects, vfs_filter_obj_t* objs)
{
    vfs_object_t *root = &dev->rootfs;
    vfs_object_t *nodes = (vfs_object_t *)root->data.obj;

    unsigned i, cnt;
    for (i = 0, cnt = 0; cnt < max_objects && i < root->eparam[0]; i++) {
        if (fnmatch(filter, nodes[i].full_path, FNM_NOESCAPE) == 0) {
            objs[cnt].fullpath = nodes[i].full_path;
            cnt++;
        }
    }

    return cnt;
}

int _usdr_device_vfs_get_by_path(device_t *base, const char* filter, pusdr_vfs_obj_t *obj)
{
    vfs_object_t *root = &base->rootfs;
    vfs_object_t *nodes = (vfs_object_t *)root->data.obj;

    for (unsigned i = 0; i < base->rootfs.eparam[0]; i++) {
        if (fnmatch(filter, nodes[i].full_path, FNM_NOESCAPE) == 0) {
            *obj = &nodes[i];
            return 0;
        }
    }

    USDR_LOG("UDEV", USDR_LOG_NOTE, "vfs '%s' not found!\n", filter);
    return -ENOENT;
}


int usdr_device_vfs_obj_val_set_by_path(pdevice_t dev, const char* fullpath, uint64_t value)
{
    pusdr_vfs_obj_t pobj;
    int res = dev->vfs_get_single_object(dev, fullpath, &pobj);
    if (res)
        return res;

    return usdr_device_vfs_obj_val_set(dev, pobj, value);
}

int usdr_device_vfs_obj_val_get_u64(pdevice_t dev, const char* fullpath, uint64_t *ovalue)
{
    pusdr_vfs_obj_t pobj;
    int res = dev->vfs_get_single_object(dev, fullpath, &pobj);
    if (res)
        return res;

    return usdr_device_vfs_obj_val_get(dev, pobj, ovalue);
}


int _oapi_vfs_set_i64_func(vfs_object_t* obj, uint64_t value)
{
    usdr_vfs_obj_ops_t* ops = (usdr_vfs_obj_ops_t* )obj->data.obj;
    return ops->val_set(obj->object, obj, value);
}

int _oapi_vfs_get_i64_func(vfs_object_t* obj, uint64_t* ovalue)
{
    usdr_vfs_obj_ops_t* ops = (usdr_vfs_obj_ops_t* )obj->data.obj;
    return ops->val_get(obj->object, obj, ovalue);
}

int usdr_vfs_obj_param_init_array_param(pdevice_t dev,
                                        void *param,
                                        const usdr_dev_param_func_t* params,
                                        unsigned count)
{
    unsigned i;
    int res;

    for (i = 0; i < count; i++) {
        res = vfs_add_obj_i64(&dev->rootfs,
                              params[i].fullpath,
                              param == NULL ? dev : param,
                              (intptr_t)&params[i].ops,
                              &_oapi_vfs_set_i64_func,
                              &_oapi_vfs_get_i64_func);
        if (res)
            return res;
    }

    return 0;
}
