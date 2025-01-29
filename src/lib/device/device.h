// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_H
#define DEVICE_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>

#include "device_vfs.h"

/** @file Generic device functions */
struct device_id {
    uint8_t d[16];
};
typedef struct device_id device_id_t;

struct device;
typedef struct device device_t;

typedef uint64_t stream_id_t;

struct stream_handle;
typedef struct stream_handle stream_handle_t;

typedef vfs_object_t *pusdr_vfs_obj_t;

struct vfs_filter_obj {
    const char* fullpath;
};
typedef struct vfs_filter_obj vfs_filter_obj_t;


struct device {
    lldev_t dev;              ///< Underlying lowlevel device

    vfs_object_t rootfs;      ///< All

    int (*initialize)(device_t *udev, unsigned pcount, const char** devparam, const char** devval);
    void (*destroy)(device_t *udev);

    // Highlevel stream operations
    int (*create_stream)(device_t* dev, const char* sid, const char* dformat,
                         uint64_t channels, unsigned pktsyms,
                         unsigned flags, stream_handle_t** out_handle);
    int (*unregister_stream)(device_t* dev, stream_handle_t* stream);


    // Sync / HW timer operation
    int (*timer_op)(device_t* dev,
                    stream_handle_t** pstreams,
                    unsigned stream_count,
                    const char* sync_op);

    // VFS filter operation
    int (*vfs_get_single_object)(device_t* dev, const char* fullpath, pusdr_vfs_obj_t* obj);
    int (*vfs_filter)(device_t* dev, const char* filter, unsigned max_objects, vfs_filter_obj_t* objs);
};

typedef struct device device_t;
typedef device_t* pdevice_t;

const char* usdr_device_id_to_str(device_id_t devid);

/* Creates /??/ structure with interal descriptions for basic bus functional mappings */
struct device_factory_ops {
    int (*create)(lldev_t dev, device_id_t devid);
};

int usdr_device_base_create(pdevice_t dev, lldev_t lldev);
int usdr_device_base_destroy(pdevice_t dev);


int usdr_device_create(lldev_t dev, device_id_t devid);
int usdr_device_destroy(pdevice_t udev);

int usdr_device_register(device_id_t devid, const struct device_factory_ops* ops);

/* initializes internal device list */
int usdr_device_init();


// VFS operation (deprecated)
typedef struct vfs_constant_i64 usdr_dev_param_constant_t;

int usdr_device_vfs_filter(pdevice_t dev, const char* filter, unsigned max_objects, vfs_filter_obj_t* objs);

int usdr_device_vfs_obj_val_get_u64(pdevice_t dev, const char* fullpath, uint64_t *ovalue);
int usdr_device_vfs_obj_val_set_by_path(pdevice_t dev, const char* fullpath, uint64_t ovalue);

struct usdr_core_info {
    const char* path;
    unsigned busno;
    unsigned core;
    unsigned base;
    int irq;
};
typedef struct usdr_core_info usdr_core_info_t;

int usdr_device_vfs_link_get_corenfo(pdevice_t dev, const char* fullpath, const char *linkpath, usdr_core_info_t *nfo);

static inline int usdr_device_vfs_obj_val_get_u32(pdevice_t dev, const char* fullpath, uint32_t *ovalue)
{
    uint64_t v;
    int res = usdr_device_vfs_obj_val_get_u64(dev, fullpath, &v);
    if (res)
        return res;

    *ovalue = v;
    return 0;
}

static inline int usdr_device_vfs_obj_val_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value)
{
    return obj->ops.si64 ? obj->ops.si64(obj, value) : -ENOENT;
}

static inline int usdr_device_vfs_obj_val_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue)
{
    return obj->ops.gi64 ? obj->ops.gi64(obj, ovalue) : -ENOENT;
}


// Old API
typedef int (*usdr_vfs_set_func_t)(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
typedef int (*usdr_vfs_get_func_t)(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

struct usdr_vfs_obj_ops {
    usdr_vfs_set_func_t val_set;
    usdr_vfs_get_func_t val_get;
};
typedef struct usdr_vfs_obj_ops usdr_vfs_obj_ops_t;

struct usdr_dev_param_func {
    const char* fullpath;
    usdr_vfs_obj_ops_t ops;
};
typedef struct usdr_dev_param_func usdr_dev_param_func_t;

int usdr_vfs_obj_param_init_array_param(pdevice_t dev,
                                        void *param,
                                        const usdr_dev_param_func_t* params,
                                        unsigned count);

static inline int usdr_vfs_obj_param_init_array(pdevice_t dev,
                                                const usdr_dev_param_func_t* params,
                                                unsigned count) {
    return usdr_vfs_obj_param_init_array_param(dev, NULL, params, count);
}


#endif
