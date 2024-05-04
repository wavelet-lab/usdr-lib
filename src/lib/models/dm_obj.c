// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "dm_obj.h"

void usdr_dmo_init(usdr_dm_obj_t* obj, const struct usdr_dm_obj_ops* ops)
{
    obj->ops = ops;
    obj->next = obj->prev = obj;
}

void usdr_dmo_unbind(usdr_dm_obj_t* obj)
{
    obj->prev->next = obj->next;
    obj->next->prev = obj->prev;

    obj->next = obj->prev = obj;
}

void usdr_dmo_destroy(usdr_dm_obj_t* obj)
{
    usdr_dmo_unbind(obj);
    obj->ops->release(obj);
}

void usdr_dmo_append(usdr_dm_obj_t* head, usdr_dm_obj_t* o)
{
    o->prev = head->prev;
    o->next = head;

    head->prev->next = o;
    head->prev = o;
}
