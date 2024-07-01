// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_H
#define DEVICE_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>

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

struct device_impl_base;
typedef struct device_impl_base device_impl_base_t;

struct usdr_vfs_obj;
typedef struct usdr_vfs_obj usdr_vfs_obj_t;
typedef usdr_vfs_obj_t* pusdr_vfs_obj_t;

struct vfs_filter_obj;
typedef struct vfs_filter_obj vfs_filter_obj_t;

struct device {
    device_impl_base_t* impl; ///< General basic device data
    lldev_t dev; ///< Underlying lowlevel device

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
    int (*vfs_get_single_object)(device_t* dev, const char* fullpath, usdr_vfs_obj_t** obj);
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



#endif
