// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "device_vfs.h"
#include <usdr_logging.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fnmatch.h>

#define STD_FOLDER_QTY 16

enum {
    RP_USED = 0,
    RP_TOTAL = 1,
    RP_UNUSED = 2,
};

enum {
    MAX_PER_FOLDER = 0xffff,
};

int vfs_folder_init(vfs_object_t* o, const char* path, void* user)
{
    o->type = VFST_FOLDER;
    o->amask = 0;
    o->eparam[RP_USED] = 0;
    o->eparam[RP_TOTAL] = STD_FOLDER_QTY;
    o->eparam[RP_UNUSED] = 0;
    o->object = user;

    memset(&o->ops, 0, sizeof(o->ops));

    o->data.obj = malloc(sizeof(vfs_object_t) * STD_FOLDER_QTY);
    if (o->data.obj == NULL)
        return -ENOMEM;

    strncpy(o->full_path, path, sizeof(o->full_path));

    return 0;
}

void vfs_folder_destroy(vfs_object_t* o)
{
    o->eparam[RP_USED] = 0;
    o->eparam[RP_TOTAL] = 0;
    free(o->data.obj);
    o->data.obj = NULL;
}

static int _vfs_reserve(vfs_object_t* root, unsigned extra)
{
    if (root->type != VFST_FOLDER) {
        return -EINVAL;
    }

    unsigned avail = root->eparam[RP_TOTAL] - root->eparam[RP_USED];
    if (avail >= extra)
        return 0;

    if (root->eparam[RP_TOTAL] == MAX_PER_FOLDER) {
        return -E2BIG;
    }

    unsigned needq = extra - avail;
    unsigned newsz = 3 * root->eparam[RP_TOTAL] / 2;

    if (needq > newsz)
        newsz = needq;

    if (newsz > MAX_PER_FOLDER)
        newsz = MAX_PER_FOLDER;

    void* nobj = realloc(root->data.obj, newsz * sizeof(vfs_object_t));
    if (nobj == NULL)
        return -ENOMEM;

    root->eparam[RP_TOTAL] = newsz;
    root->data.obj = nobj;
    return 0;
}

static int _vfs_alloc_object(vfs_object_t* root, vfs_object_t** newobj, uint8_t type, const char* path)
{
    int res = _vfs_reserve(root, 1);
    if (res)
        return res;

    vfs_object_t* obj = &((vfs_object_t*)root->data.obj)[root->eparam[RP_USED]++];
    memset(obj, 0, sizeof(vfs_object_t));

    obj->type = type;
    strncpy(obj->full_path, path, sizeof(obj->full_path));

    *newobj = obj;
    return 0;
}

// constant functions
static int _vfs_const_set_i64_func(vfs_object_t* obj, uint64_t value)
{
    return -EPERM;
}

static int _vfs_const_set_str_func(vfs_object_t* obj, const char* str)
{
    return -EPERM;
}

static int _vfs_const_set_ai64_func(vfs_object_t* obj, unsigned count, const uint64_t* value)
{
    return -EPERM;
}


int _vfs_const_i64_get_i64_func(vfs_object_t* obj, uint64_t* ovalue)
{
    *ovalue = obj->data.i64; return 0;
}

int _vfs_i64_get_str_func(vfs_object_t* obj, unsigned max_str, char* stor)
{
    uint64_t val;
    int res = obj->ops.gi64(obj, &val);
    if (res)
        return res;
    snprintf(stor, max_str, "%lld", (long long)val);
    return 0;
}

int _vfs_i64_get_ai64_func(vfs_object_t* obj, unsigned maxcnt, uint64_t* ovalue)
{
    if (maxcnt == 0)
        return 0;
    int res = obj->ops.gi64(obj, ovalue);
    if (res)
        return res;
    return 1;
}

int _vfs_const_str_get_str_func(vfs_object_t* obj, unsigned max_str, char* stor)
{
    strncpy(stor, obj->data.str, max_str);
    return 0;
}

int _vfs_str_get_i64_func(vfs_object_t* obj, uint64_t* ovalue) {
    char data[32];
    int res = obj->ops.gstr(obj, sizeof(data), data);
    if (res)
        return res;
    data[31] = 0;
    char *eptr = NULL;
    long long int v = strtoll(data, &eptr, 10);
    *ovalue = v;
    return (eptr && *eptr == 0) ? 0 : -EINVAL;
}

int _vfs_str_get_ai64_func(vfs_object_t* obj, unsigned maxcnt, uint64_t* ovalue)
{
    return -EINVAL;
}

int _vfs_ai64_get_i64_func(vfs_object_t* obj, uint64_t* ovalue)
{
    return -EINVAL;
}

int _vfs_ai64_get_str_func(vfs_object_t* obj, unsigned max_str, char* stor)
{
    return -EINVAL;
}



int vfs_add_const_i64_vec(vfs_object_t* root, const struct vfs_constant_i64* params, unsigned count)
{
    int res = _vfs_reserve(root, count);
    if (res)
        return res;

    for (unsigned j = 0; j < count; j++) {
        vfs_object_t* no;
        res = _vfs_alloc_object(root, &no, VFST_I64, params[j].fullpath);
        if (res)
            return res;

        no->ops.si64 = &_vfs_const_set_i64_func;
        no->ops.sstr = &_vfs_const_set_str_func;
        no->ops.sai64 = &_vfs_const_set_ai64_func;

        no->ops.gi64 = &_vfs_const_i64_get_i64_func;
        no->ops.gstr = &_vfs_i64_get_str_func;
        no->ops.gai64 = &_vfs_i64_get_ai64_func;

        no->data.i64 = params[j].value;
    }
    return 0;
}


int vfs_add_const_str_vec(vfs_object_t* root, const struct vfs_constant_str* params, unsigned count)
{
    int res = _vfs_reserve(root, count);
    if (res)
        return res;

    for (unsigned j = 0; j < count; j++) {
        vfs_object_t* no;
        res = _vfs_alloc_object(root, &no, VFST_I64, params[j].fullpath);
        if (res)
            return res;

        no->ops.si64 = &_vfs_const_set_i64_func;
        no->ops.sstr = &_vfs_const_set_str_func;
        no->ops.sai64 = &_vfs_const_set_ai64_func;

        no->ops.gi64 = &_vfs_const_i64_get_i64_func;
        no->ops.gstr = &_vfs_i64_get_str_func;
        no->ops.gai64 = &_vfs_i64_get_ai64_func;

        no->data.str = (char*)params[j].value;
    }
    return 0;
}

int vfs_add_obj_i64(vfs_object_t* root, const char* fullpath, void* obj, uint64_t defval, vfs_set_i64_func_t fs, vfs_get_i64_func_t fg)
{
    vfs_object_t* no;
    int res = _vfs_alloc_object(root, &no, VFST_I64, fullpath);
    if (res)
        return res;

    no->object = obj;
    no->data.i64 = defval;

    no->ops.si64 = fs;
    no->ops.sstr = NULL;  // TODO
    no->ops.sai64 = NULL; // TODO

    no->ops.gi64 = fg;
    no->ops.gstr = &_vfs_i64_get_str_func;
    no->ops.gai64 = &_vfs_i64_get_ai64_func;

    return 0;
}
