// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef _LIBUSB_GENERIC_H
#define _LIBUSB_GENERIC_H

#include <stdlib.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <string.h>
#include <semaphore.h>
#include <time.h>

#include <usdr_logging.h>

#include "../device/device.h"
#include "libusb_vidpid_map.h"

static int libusb_to_errno(int libusberr)
{
    switch (libusberr) {
    case LIBUSB_SUCCESS:
        return 0;

    case LIBUSB_ERROR_IO:
        return -EIO;

    case LIBUSB_ERROR_INVALID_PARAM:
        return -EINVAL;

    case LIBUSB_ERROR_ACCESS:
        return -EPERM;

    case LIBUSB_ERROR_NO_DEVICE:
        return -ENODEV;

    case LIBUSB_ERROR_NOT_FOUND:
        return -ENXIO;

    case LIBUSB_ERROR_BUSY:
        return -EBUSY;

    case LIBUSB_ERROR_TIMEOUT:
        return -ETIMEDOUT;

    case LIBUSB_ERROR_OVERFLOW:
        return -EOVERFLOW;

    case LIBUSB_ERROR_PIPE:
        return -EPIPE;

    case LIBUSB_ERROR_INTERRUPTED:
        return -EINTR;

    case LIBUSB_ERROR_NO_MEM:
        return -ENOMEM;

    case LIBUSB_ERROR_NOT_SUPPORTED:
        return -EOPNOTSUPP;

    case LIBUSB_ERROR_OTHER:
        return -ESRCH; //TODO find better;
    };
    return -EFAULT;
}

#define MAX_DEVIDS 15

struct matched_devs {
    libusb_device* dev;
    unsigned devid;
    unsigned uuid_idx;
    unsigned bdevproto;
    char devid_s[MAX_DEVIDS];
    sdr_type_t sdrtype;
};

struct usb_filtering_params
{
    int usb_bus;
    int usb_port;
    int usb_addr;
};
typedef struct usb_filtering_params usb_filtering_params_t;

struct libusb_generic_dev {
    libusb_context* ctx;
    libusb_device_handle* dh;
    unsigned usb_speed;

    char name[128];
    char devid_str[36];
    device_id_t devid;
    sdr_type_t sdrtype;

    pthread_t io_thread;
    bool stop;
};
typedef struct libusb_generic_dev libusb_generic_dev_t;

int libusb_generic_plugin_discovery(unsigned pcount, const char** filterparams,
                                    const char** filtervals,
                                    unsigned maxbuf, char* outarray,
                                    const usb_pidvid_map_t *known_devs, unsigned known_devs_cnt,
                                    const char* busname);

int libusb_generic_plugin_create(unsigned pcount, const char** devparam,
                                 const char** devval,
                                 const usb_pidvid_map_t *known_devs, unsigned known_devs_cnt,
                                 const char* busname, libusb_generic_dev_t* odev);



static int libusb_generic_prepare_transfer(libusb_generic_dev_t* d,
                                           void* user_data,
                                           unsigned endpoint,
                                           enum libusb_transfer_type endpoint_type,
                                           struct libusb_transfer **ot,
                                           unsigned char* buffer_top,
                                           unsigned buffersz,
                                           unsigned count,
                                           libusb_transfer_cb_fn cb)
{
    unsigned i;
    for (i = 0; i < count; i++) {
        ot[i] = libusb_alloc_transfer(0);
        if (ot[i] == 0)
            return -ENOMEM; //TODO CLEANUP!!!!!

        ot[i]->dev_handle = d->dh;
        ot[i]->flags = 0;
        ot[i]->endpoint = endpoint;
        ot[i]->type = endpoint_type;
        ot[i]->timeout = 0; //TODO!!!
        ot[i]->buffer = buffer_top + i * buffersz;
        ot[i]->length = 0;
        ot[i]->callback = cb;
        ot[i]->user_data = user_data;
    }

    return 0;
}

int libusb_generic_create_thread(libusb_generic_dev_t *dev);
int libusb_generic_stop_thread(libusb_generic_dev_t *dev);


// Return -errno if fails
int sem_wait_ex(sem_t *s, int64_t timeout_ns);


// Buffers
struct buffers;
struct buffer_discriptor
{
    struct buffers* b;

    unsigned bno;
    unsigned buffer_sz; //Actual number of transfer
};

#define BUFFERS_MAX_TRANS 32

struct buffers
{
    sem_t buf_ready;

    uint8_t* rqueuebuf_ptr; // cache aligned pointer to rx_queuebuf

    unsigned allocsz;
    unsigned allocsz_rounded; // rounded up buffer to the maximum USB Transfer size

    uint32_t buf_max;       // total number of user buffer
    uint32_t buf_available; // number of buffers available for communication

    uint32_t bufno_prod;
    uint32_t bufno_cons;

    int fd_event; // if != -1 use event signaling instead of semaphores
    int auto_restart; // auto restart (for IN) to fill up the buffer
    int stop;

    struct buffer_discriptor *bd;

    //
    struct libusb_transfer *transfers[BUFFERS_MAX_TRANS];
    unsigned transfers_count;

    void* on_buffer_param;
    void (*on_buffer)(void* param, struct buffer_discriptor * bd);
};

int buffers_init(struct buffers* rb, unsigned max, unsigned zerosemval, bool has_event);
int buffers_realloc(struct buffers* rb, unsigned allocsz);
void buffers_deinit(struct buffers* rb);
void buffers_reset(struct buffers* rb);

unsigned buffers_produce(struct buffers *prxb);
unsigned buffers_consume(struct buffers *prxb);

int buffers_ready_wait(struct buffers *rxb, unsigned timeout_us);
int buffers_ready_post(struct buffers *rxb);

unsigned buffers_available_get(struct buffers *prxb);
unsigned buffers_available_post(struct buffers *prxb);

static inline void* buffers_get_ptr(struct buffers *prxb, unsigned bno) {
    return prxb->rqueuebuf_ptr + bno * prxb->allocsz_rounded;
}

// TODO: get rid off ugly call
unsigned _buffers_prod_get_nolock(struct buffers *prxb);

int buffers_usb_transfer_post(struct buffers *prxb, unsigned buffer_idx, unsigned length,
                              unsigned transfer_idx);


void LIBUSB_CALL libusb_transfer_buffers_cb(struct libusb_transfer *transfer);

// TODO: on auto resubmit mode max_buffs must be more than max_reqs
int buffers_usb_init(libusb_generic_dev_t* gdev, struct buffers *prxb,
                     unsigned max_reqs, unsigned max_buffs, unsigned max_blocksize,
                     unsigned endpoint, bool eventfd_ntfy);

int buffers_usb_free(struct buffers *prxb);

#endif
