// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "device_vfs.h"
#include "device_impl.h"
#include <usdr_logging.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fnmatch.h>


int usdr_device_vfs_add(pdevice_t dev, pusdr_vfs_obj_t obj)
{
    usdr_vfs_obj_base_t* o = (usdr_vfs_obj_base_t*)obj;
    device_impl_base_t *base = dev->impl;
    if (o->uid != base->objcount)
        return -EINVAL;

    if (base->objcount == base->objmax) {
        size_t newsz = base->objcount * 3 / 2;
        dev->impl = base = (device_impl_base_t*)realloc(base, sizeof(device_impl_base_t) + newsz * sizeof(usdr_vfs_obj_base_t*));
        base->objmax = newsz;

        USDR_LOG("UDEV", USDR_LOG_NOTE, "vfs increasing storage to %zd\n", newsz);
    }

    base->objlist[base->objcount] = o;
    base->objcount++;

    USDR_LOG("UDEV", USDR_LOG_NOTE, "vfs '%s' @ %u was registered.",
             o->fullpath, o->uid);
    return 0;
}

int usdr_device_vfs_obj_val_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    struct usdr_vfs_obj_base* o = (struct usdr_vfs_obj_base* )obj;
    return o->ops->val_set ? o->ops->val_set(ud, obj, value) : -ENOENT;
}

int usdr_device_vfs_obj_val_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    struct usdr_vfs_obj_base* o = (struct usdr_vfs_obj_base* )obj;
    return o->ops->val_get ? o->ops->val_get(ud, obj, ovalue) : -ENOENT;
}


static
int usdr_vfs_obj_constant_val_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    USDR_LOG("UVFS", USDR_LOG_ERROR, "vfs uid %d is constant!\n",
             ((struct usdr_vfs_obj_base*)obj)->uid);
    return -EPERM;
}

static
int usdr_vfs_obj_constant_val_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue)
{
    struct usdr_vfs_obj_constant* c = (struct usdr_vfs_obj_constant*)obj;
    *ovalue = c->value;

    return 0;
}

int usdr_device_vfs_obj_val_get_u64(pdevice_t dev, const char* fullpath, uint64_t *ovalue)
{
    pusdr_vfs_obj_t pobj;
    //int res = usdr_device_vfs_get_by_path(dev, fullpath, &pobj);
    int res = dev->vfs_get_single_object(dev, fullpath, &pobj);
    if (res)
        return res;

    return usdr_device_vfs_obj_val_get(dev, pobj, ovalue);
}

int usdr_device_vfs_obj_val_get_u32(pdevice_t dev, const char* fullpath, uint32_t *ovalue)
{
    uint64_t v;
    int res = usdr_device_vfs_obj_val_get_u64(dev, fullpath, &v);
    if (res)
        return res;

    *ovalue = v;
    return 0;
}

int usdr_device_vfs_obj_val_set_by_path(pdevice_t dev, const char* fullpath, uint64_t value)
{
    pusdr_vfs_obj_t pobj;
//    int res = usdr_device_vfs_get_by_path(dev, fullpath, &pobj);
    int res = dev->vfs_get_single_object(dev, fullpath, &pobj);
    if (res)
        return res;

    return usdr_device_vfs_obj_val_set(dev, pobj, value);
}


static
const struct usdr_vfs_obj_ops s_usdr_vfs_obj_constant_ops = {
    usdr_vfs_obj_constant_val_set,
    usdr_vfs_obj_constant_val_get,
};


int usdr_vfs_obj_const_init_add(pdevice_t dev,
                                struct usdr_vfs_obj_constant *co,
                                const char* path,
                                usdr_vfs_obj_uid_t uid,
                                uint64_t value)
{
    co->base.ops = &s_usdr_vfs_obj_constant_ops;
    co->base.fullpath = path;
    co->base.param = NULL;
    co->base.uid = uid;
    co->base.defaccesslist = ~0u;
    co->value = value;

    return usdr_device_vfs_add(dev, (pusdr_vfs_obj_t)co);
}

int usdr_vfs_obj_const_init_array(pdevice_t dev,
                                  usdr_vfs_obj_uid_t uid_first,
                                  usdr_vfs_obj_constant_t* objstorage,
                                  const usdr_dev_param_constant_t* params,
                                  unsigned count)
{
    unsigned i;
    int res;

    for (i = 0; i < count; i++) {
        res = usdr_vfs_obj_const_init_add(dev, &objstorage[i], params[i].fullpath, uid_first + i, params[i].value);
        if (res)
            return res;
    }

    return 0;
}

int usdr_vfs_obj_param_init_array_param(pdevice_t dev,
                                        usdr_vfs_obj_uid_t uid_first,
                                        usdr_vfs_obj_base_t *objstorage,
                                        void *param,
                                        const usdr_dev_param_func_t* params,
                                        unsigned count)
{
    unsigned i;
    int res;

    for (i = 0; i < count; i++) {
        usdr_vfs_obj_base_t* pob = &objstorage[i];
        pob->ops = &params[i].ops;
        pob->fullpath = params[i].fullpath;
        pob->param = param;
        pob->uid = uid_first + i;
        pob->defaccesslist = ~0u;

        res = usdr_device_vfs_add(dev, (pusdr_vfs_obj_t)pob);
        if (res)
            return res;
    }

    return 0;
}
