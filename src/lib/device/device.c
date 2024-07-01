// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "device.h"
#include <string.h>
#include <usdr_logging.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "device_impl.h"
#include "device_vfs.h"
#include <fnmatch.h>

static int _usdr_device_vfs_get_by_path(struct device_impl_base *base, const char* fullpath, pusdr_vfs_obj_t *obj)
{
    for (unsigned i = 0; i < base->objcount; i++) {
        if (fnmatch(base->objlist[i]->fullpath, fullpath, 0) == 0) {
            *obj = (pusdr_vfs_obj_t)base->objlist[i];
            return 0;
        }
    }

    USDR_LOG("UDEV", USDR_LOG_NOTE, "vfs '%s' not found!\n", fullpath);
    return -ENOENT;
}

static int _obj_get_single_object_impl(device_t* dev, const char* fullpath, usdr_vfs_obj_t** obj)
{
    return _usdr_device_vfs_get_by_path(dev->impl, fullpath, obj);
}

int usdr_device_base_create(pdevice_t dev, lldev_t lldev)
{
    device_impl_base_t *b = (device_impl_base_t *)malloc(sizeof(device_impl_base_t) + MAX_OBJECTS_DEF * sizeof(usdr_vfs_obj_base_t*));
    b->objmax = MAX_OBJECTS_DEF;
    b->objcount = 0;

    dev->impl = b;
    dev->dev = lldev;
    dev->initialize = NULL;
    dev->destroy = NULL;
    dev->create_stream = NULL;
    dev->unregister_stream = NULL;
    dev->timer_op = NULL;
    dev->vfs_get_single_object = &_obj_get_single_object_impl;
    dev->vfs_filter = &usdr_device_vfs_filter;
    return 0;
}

int usdr_device_base_destroy(pdevice_t dev)
{
    free(dev->impl);
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
   //USDR_LOG("UDEV", USDR_LOG_ERROR, "usdr_device_destroy() not implimented!");
   return 0;
}


void __attribute__ ((constructor(120))) setup_dev(void) {
    usdr_device_init();
}

