// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DM_OBJ_H
#define DM_OBJ_H

struct usdr_dm_obj;
typedef struct usdr_dm_obj usdr_dm_obj_t;

struct usdr_dm_obj_ops {
    void (*release)(usdr_dm_obj_t* obj);
};
typedef struct usdr_dm_obj_ops usdr_dm_obj_ops_t;

struct usdr_dm_obj {
    const struct usdr_dm_obj_ops* ops;
    usdr_dm_obj_t* next;
    usdr_dm_obj_t* prev;
};

void usdr_dmo_init(usdr_dm_obj_t* obj, const usdr_dm_obj_ops_t* ops);
void usdr_dmo_destroy(usdr_dm_obj_t* obj);

void usdr_dmo_append(usdr_dm_obj_t* head, usdr_dm_obj_t* o);
void usdr_dmo_unbind(usdr_dm_obj_t* obj);

#endif
