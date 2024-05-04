// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_VFS_H
#define DEVICE_VFS_H

#include "device.h"

// Device has VFS based object access
typedef int (*usdr_vfs_set_func_t)(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
typedef int (*usdr_vfs_get_func_t)(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t* ovalue);

struct usdr_vfs_obj_ops {
    usdr_vfs_set_func_t val_set;
    usdr_vfs_get_func_t val_get;
};
typedef struct usdr_vfs_obj_ops usdr_vfs_obj_ops_t;
typedef uint32_t usdr_vfs_obj_uid_t;

struct usdr_vfs_obj_base {
    const struct usdr_vfs_obj_ops* ops;
    const char* fullpath;
    void* param;
    usdr_vfs_obj_uid_t uid;
    uint32_t defaccesslist;
};
typedef struct usdr_vfs_obj_base usdr_vfs_obj_base_t;

struct usdr_vfs_obj_constant {
    struct usdr_vfs_obj_base base;
    uint64_t value;
};
typedef struct usdr_vfs_obj_constant usdr_vfs_obj_constant_t;

static inline
usdr_vfs_obj_ops_t* usdr_vfs_obj_ops(pusdr_vfs_obj_t obj) {
    return *(usdr_vfs_obj_ops_t**)obj;
}

int usdr_vfs_obj_const_init_add(pdevice_t dev,
                                struct usdr_vfs_obj_constant *co,
                                const char* path,
                                usdr_vfs_obj_uid_t uid,
                                uint64_t value);

struct usdr_dev_param_constant {
    const char* fullpath;
    uint64_t value;
};

typedef struct usdr_dev_param_constant usdr_dev_param_constant_t;
int usdr_vfs_obj_const_init_array(pdevice_t dev,
                                  usdr_vfs_obj_uid_t uid_first,
                                  usdr_vfs_obj_constant_t *objstorage,
                                  const usdr_dev_param_constant_t* params,
                                  unsigned count);

struct usdr_dev_param_func {
    const char* fullpath;
    usdr_vfs_obj_ops_t ops;
};
typedef struct usdr_dev_param_func usdr_dev_param_func_t;

int usdr_vfs_obj_param_init_array_param(pdevice_t dev,
                                        usdr_vfs_obj_uid_t uid_first,
                                        usdr_vfs_obj_base_t *objstorage,
                                        void *param,
                                        const usdr_dev_param_func_t* params,
                                        unsigned count);
static inline
int usdr_vfs_obj_param_init_array(pdevice_t dev,
                                  usdr_vfs_obj_uid_t uid_first,
                                  usdr_vfs_obj_base_t *objstorage,
                                  const usdr_dev_param_func_t* params,
                                  unsigned count) {
    return usdr_vfs_obj_param_init_array_param(dev, uid_first, objstorage, NULL, params, count);
}

// Add object to internal global tree structure
int usdr_device_vfs_add(pdevice_t dev, pusdr_vfs_obj_t obj);

int usdr_device_vfs_obj_val_set(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t value);
int usdr_device_vfs_obj_val_get(pdevice_t ud, pusdr_vfs_obj_t obj, uint64_t *ovalue);


int usdr_device_vfs_obj_val_get_u32(pdevice_t dev, const char* fullpath, uint32_t *ovalue);
int usdr_device_vfs_obj_val_get_u64(pdevice_t dev, const char* fullpath, uint64_t *ovalue);
int usdr_device_vfs_obj_val_set_by_path(pdevice_t dev, const char* fullpath, uint64_t ovalue);

#endif
