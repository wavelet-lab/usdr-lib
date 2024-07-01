// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DM_DEV_H
#define DM_DEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <usdr_port.h>

struct dm_dev;
typedef struct dm_dev dm_dev_t;
typedef dm_dev_t* pdm_dev_t;

int usdr_dmd_create_string(const char* connection_string, pdm_dev_t* odev);
int usdr_dmd_close(pdm_dev_t dev);
int usdr_dmd_discovery(const char* filer_string, unsigned max_buf, char* devlist);
int usdr_dmd_create_webusb(unsigned vidpid, void* webops, uintptr_t param, pdm_dev_t* odev);

struct dme_param {
    const char* fullpath;
};
typedef struct dme_param dme_param_t;

int usdr_dme_filter(pdm_dev_t dev, const char* pattern, unsigned count, dme_param_t* objs);

int usdr_dme_get_u32(pdm_dev_t dev, const char* path, uint32_t *oval);
int usdr_dme_get_uint(pdm_dev_t dev, const char* path, uint64_t *oval);
int usdr_dme_get_string(pdm_dev_t dev, const char* path, const char** oval);

int usdr_dme_set_uint(pdm_dev_t dev, const char* path, uint64_t val);
int usdr_dme_set_string(pdm_dev_t dev, const char* path, const char* val);


struct dme_findsetv_data {
    const char* name;
    uint64_t value;
    bool ignore;
    bool stopOnFail;
};

int usdr_dme_findsetv_uint(pdm_dev_t dev, const char* directory, unsigned count,
                           const struct dme_findsetv_data* pdata);

#if 0
typedef int (*entity_cb_filer_t)(pdm_dev_t, const dm_dev_entity_t*, void*, uint64_t, uint64_t*);
/* modify value before the call */
int usdr_dme_filter(pdm_dev_t dev, dm_dev_entity_t entity, entity_cb_filer_t fn, void* param);

typedef void (*entity_cb_notify_t)(pdm_dev_t, const dm_dev_entity_t*, void*, uint64_t);
/* notify if the value of the paramter was changed */
int usdr_dme_notify(pdm_dev_t dev, dm_dev_entity_t entity, entity_cb_notify_t fn, void* param);

/* free complex entity, or do nothing in simple case */
void usdr_dme_release(pdm_dev_t dev, dm_dev_entity_t entity);
#endif

#ifdef __cplusplus
}
#endif


#endif
