// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEVICE_VFS_H
#define DEVICE_VFS_H

#include <stdint.h>

enum vfs_type {
    VFST_LINK = 'l',
    VFST_FOLDER = 'f',

    VFST_I64 = 'i',
    VFST_STR = 's',
    VFST_ARR_I64 = 'a',
};

enum vfs_constants {
    VFS_MAX_PATH = 128,
};

union vfs_variant {
    int64_t i64;
    char* str;
    void* obj;
};

struct vfs_object;
typedef struct vfs_object vfs_object_t;


typedef int (*vfs_set_i64_func_t)(vfs_object_t* obj, uint64_t value);
typedef int (*vfs_get_i64_func_t)(vfs_object_t* obj, uint64_t* ovalue);

typedef int (*vfs_set_str_func_t)(vfs_object_t* obj, const char* str);
typedef int (*vfs_get_str_func_t)(vfs_object_t* obj, unsigned max_str, char* stor);

typedef int (*vfs_set_ai64_func_t)(vfs_object_t* obj, unsigned count, const uint64_t* value);
typedef int (*vfs_get_ai64_func_t)(vfs_object_t* obj, unsigned maxcnt, uint64_t* ovalue);

struct vfs_ops {
    vfs_set_i64_func_t si64;
    vfs_get_i64_func_t gi64;
    vfs_set_str_func_t sstr;
    vfs_get_str_func_t gstr;
    vfs_set_ai64_func_t sai64;
    vfs_get_ai64_func_t gai64;
};


struct vfs_object {
    uint8_t type;
    uint8_t amask;
    uint16_t eparam[3]; // Object specific paramenets
    void* object;       // User associated object with the vfs

    struct vfs_ops ops;
    union vfs_variant data;

    char full_path[VFS_MAX_PATH];
};
typedef struct vfs_object vfs_object_t;

int vfs_folder_init(vfs_object_t* o, const char* path, void* user);
void vfs_folder_destroy(vfs_object_t* o);

struct vfs_constant_i64 {
    const char* fullpath;
    uint64_t value;
};

int vfs_add_const_i64_vec(vfs_object_t* root, const struct vfs_constant_i64* params, unsigned count);

static inline int vfs_add_const_i64(vfs_object_t* root, const struct vfs_constant_i64* params) {
    return vfs_add_const_i64_vec(root, params, 1);
}

struct vfs_constant_str {
    const char* fullpath;
    const char* value;
};

int vfs_add_const_str_vec(vfs_object_t* root, const struct vfs_constant_str* params, unsigned count);

static inline int vfs_add_const_str(vfs_object_t* root, const struct vfs_constant_str* params) {
    return vfs_add_const_str_vec(root, params, 1);
}

int vfs_add_obj_i64(vfs_object_t* root, const char* fullpath, void* obj, uint64_t defval, vfs_set_i64_func_t fs, vfs_get_i64_func_t fg);



#endif
