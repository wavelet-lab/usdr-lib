// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include "libusb_generic.h"
#include "libusb_vidpid_map.h"
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>

#ifdef __linux
#include <sys/eventfd.h>
static int fdevent_create(unsigned initial) { return eventfd(initial, EFD_SEMAPHORE | EFD_SEMAPHORE); }
static int fdevent_destroy(int fd) { return close(fd); }
static int fdevent_post(int fd, unsigned count) {
    uint64_t v = count;
    ssize_t r = write(fd, &v, sizeof(v));
    return (r == sizeof(v)) ? 0 : -errno;
}
static int fdevent_get(int fd, unsigned* count) {
    uint64_t v = 0;
    ssize_t r = read(fd, &v, sizeof(v));
    if (count) *count = v;
    return (r == sizeof(v)) ? 0 : -errno;
}
#else
#endif


#define MAX_DEV 64


static
unsigned find_usb_match(libusb_device **usbdev, size_t devices,
                        usb_filtering_params_t *fparams,
                        unsigned max_devs, struct matched_devs *devs,
                        const usb_pidvid_map_t *known_devices,
                        unsigned known_device_count,
                        const char* busname)
{
    struct libusb_device_descriptor desc;
    int res;
    int j;
    unsigned k = 0;

    for (unsigned i = 0; i < devices && k < max_devs; i++) {
        struct matched_devs* md = &devs[k];

        res = libusb_get_device_descriptor(usbdev[i], &desc);
        if (res)
            continue;

        j = libusb_find_dev_index_ex( busname,desc.idProduct, desc.idVendor, known_devices, known_device_count );
        if( j < 0 ) continue;
        md->uuid_idx = j;
        md->sdrtype = libusb_get_dev_sdrtype(j);

        uint8_t bus = libusb_get_bus_number(usbdev[i]);
        uint8_t port = libusb_get_port_number(usbdev[i]);
        uint8_t addr = libusb_get_device_address(usbdev[i]);

        md->dev = usbdev[i];
        md->bdevproto = desc.bDeviceProtocol;
        md->devid = (unsigned)bus * 1000000 + (unsigned)port * 1000 + addr;
        snprintf(md->devid_s, sizeof(md->devid_s), "%d/%d/%d", bus, port, addr);

        USDR_LOG("USBX", USDR_LOG_DEBUG, "checking device %04x:%04x %d/%d/%d against %d/%d/%d mask devid=%d\n",
                    desc.idVendor, desc.idProduct,
                    fparams->usb_bus, fparams->usb_port, fparams->usb_addr,
                    bus, port, addr, j);

        if ((fparams->usb_addr == -1 && fparams->usb_bus == -1 && fparams->usb_port == -1) ||
                (fparams->usb_addr == -1 && fparams->usb_bus == -1 && fparams->usb_port == port) ||
                (fparams->usb_addr == -1 && fparams->usb_bus == bus && fparams->usb_port == port) ||
                (fparams->usb_bus == bus && fparams->usb_port == port && fparams->usb_addr == addr)) {
            k++;
        }
    }
    return k;
}

static
int usb_filtering_params_parse(unsigned pcount, const char** devparam,
                               const char** devval, struct usb_filtering_params* pp)
{
    pp->usb_bus = -1;
    pp->usb_port = -1;
    pp->usb_addr = -1;

    for (unsigned k = 0; k < pcount; k++) {
        const char* val = devval[k];
        if (strcmp(devparam[k], "bus") == 0) {
            // Filter by BUS
            if (strncmp(val, "usb", 3)) {
                // Non-compatible bus
                USDR_LOG("USBX", USDR_LOG_TRACE, "`%s` ignored by USB driver\n", val);
                return -ENODEV;
            }

            const char *filter = &val[3];
            if (*filter == 0)
                continue;
            if (*filter != '@') {
                USDR_LOG("USBX", USDR_LOG_WARNING,
                         "Unsupported filter for USB: `%s`\n", filter);
                continue;
            }

            int cnt = sscanf(filter + 1, "%d/%d/%d", &pp->usb_bus, &pp->usb_port, &pp->usb_addr);
            if (cnt == EOF) {
               pp->usb_bus = -1;
               pp->usb_port = -1;
               pp->usb_addr = -1;
               continue;
            }
            USDR_LOG("USBX", USDR_LOG_INFO,
                     "Activating USB filter %d/%d/%d\n", pp->usb_bus, pp->usb_port, pp->usb_addr);
        }
    }

    return 0;
}

int libusb_generic_plugin_discovery(unsigned pcount, const char** filterparams,
                                    const char** filtervals,
                                    unsigned maxbuf, char* outarray,
                                    const usb_pidvid_map_t *known_devs, unsigned known_devs_cnt,
                                    const char* busname)
{
    struct matched_devs md[MAX_DEV];
    struct usb_filtering_params fparams;
    libusb_context* uctx;
    libusb_device **usbdev;
    ssize_t devices;
    int res;
    unsigned fcnt, i, off;

    res = usb_filtering_params_parse(pcount, filterparams, filtervals, &fparams);
    if (res)
        return res;

    if (libusb_init(&uctx)) {
        return -EFAULT;
    }

    devices = libusb_get_device_list(uctx, &usbdev);
    if (devices < 0) {
        res = -ENODEV;
        goto exit_nodev;
    }

    fcnt = find_usb_match(usbdev, devices, &fparams, MAX_DEV, md , known_devs, known_devs_cnt, busname);

    for (i = 0, off = 0; i < fcnt; i++) {
        int cap = maxbuf - off;
        int l = snprintf(outarray + off, cap, "bus=%s@%s\n", busname, md[i].devid_s);
        if (l < 0 || l > cap) {
            outarray[off] = 0;
            res = i;
            goto exit_release;
        }

        off += l;
    }

    res = i;

exit_release:
    libusb_free_device_list(usbdev, 1);
exit_nodev:
    libusb_exit(uctx);
    return res;
}



int libusb_generic_plugin_create(unsigned pcount, const char** devparam,
                                 const char** devval,
                                 const usb_pidvid_map_t *known_devs, unsigned known_devs_cnt,
                                 const char* busname, libusb_generic_dev_t* odev)
{
    struct matched_devs md;
    struct usb_filtering_params fparams;

    int res;
    int usb_speed = 0;

    libusb_context* uctx;
    libusb_device **usbdev;
    libusb_device_handle *dh;

    ssize_t devices;
    int speed;

    res = usb_filtering_params_parse(pcount, devparam, devval, &fparams);
    if (res)
        return res;

    for (unsigned ucnt = 0; ucnt < 1; ucnt++) {
        unsigned fcnt;
        libusb_device *match;

        if (libusb_init(&uctx)) {
            return -EFAULT;
        }

        devices = libusb_get_device_list(uctx, &usbdev);
        if (devices < 0) {
            libusb_exit(uctx);
            return -ENODEV;
        }

        fcnt = find_usb_match(usbdev, devices, &fparams, 1, &md , known_devs, known_devs_cnt, busname);
        if (fcnt == 0) {
            USDR_LOG("USBX", USDR_LOG_NOTE,
                     "No USB device was found to match %d/%d/%d\n",
                     fparams.usb_bus, fparams.usb_port, fparams.usb_addr);
            libusb_exit(uctx);
            return -ENODEV;
        }

        match = md.dev;
        speed = libusb_get_device_speed(match);
        switch (speed) {
        case LIBUSB_SPEED_LOW: usb_speed = 1; break;
        case LIBUSB_SPEED_FULL: usb_speed = 12; break;
        case LIBUSB_SPEED_HIGH: usb_speed = 480; break;
        case LIBUSB_SPEED_SUPER: usb_speed = 5000; break;
        }

        res = libusb_open(match, &dh);
        if (res) {
            USDR_LOG("USBX", USDR_LOG_ERROR,
                     "Unable to initialize DEVICE: error `%s` (%d)!\n",
                    libusb_strerror((enum libusb_error)res), res);
            res = libusb_to_errno(res);
            goto usbinit_fail;
            //libusb_exit(uctx);
            //return res;
        }

        if (libusb_kernel_driver_active(dh, 0)) {
            res = libusb_detach_kernel_driver(dh, 0);
            if (res) {
                USDR_LOG("USBX", USDR_LOG_DEBUG,
                        "Unable to detach kernel driver curently active for USB: `%s` (%d)!\n",
                        libusb_strerror((enum libusb_error)res), res);
            }
        }

        // USB Reset triggers full gateware reset
        res = libusb_reset_device(dh);
        if (res) {
            USDR_LOG("USBX", USDR_LOG_WARNING, "Unable to reset device: `%s` (%d)\n",
                     libusb_strerror((enum libusb_error)res), res);
        }
        usleep(50000);

        res = libusb_claim_interface(dh, 0);
        if (res) {
            USDR_LOG("USBX", USDR_LOG_ERROR,
                     "Unable to claim interface: error `%s` (%d)!",
                     libusb_strerror((enum libusb_error)res), res);
            res = libusb_to_errno(res);
            //goto cleanup_handle;
        }

        libusb_free_device_list(usbdev, 1);
        if (res) {
            USDR_LOG("USBX", USDR_LOG_ERROR,
                     "Unable to deallocate libusb context: error: %d\n", res);
            //libusb_exit(uctx);
            //return res;
            goto usbinit_fail;
        }
        break;
    }
    if (res) {
        goto usbinit_fail;
    }

    odev->ctx = uctx;
    odev->dh = dh;
    odev->usb_speed = usb_speed;
    odev->sdrtype = md.sdrtype;

    snprintf(odev->name, sizeof(odev->name) - 1, "%s@%s", busname,  md.devid_s);

    odev->devid = libusb_get_deviceid(md.uuid_idx);
    strncpy(odev->devid_str, usdr_device_id_to_str(odev->devid), sizeof(odev->devid_str) - 1);

    odev->stop = false;

    return 0;

usbinit_fail:
    //usb_context_free(ctx);
    libusb_exit(uctx);
    return res;
}



void* libusb_generic_io_thread(void *arg)
{
    libusb_generic_dev_t* dev = (libusb_generic_dev_t*)arg;
    int res = 0;

#if defined(__linux) || defined(__APPLE__)
    sigset_t set;

    pthread_setname_np(pthread_self(), "usb_io");

    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    struct sched_param shed;
    shed.sched_priority = 2;

    res = pthread_setschedparam(pthread_self(), SCHED_FIFO, &shed);
    if (res) {
        USDR_LOG("USBX", USDR_LOG_WARNING, "IO thread: Unable to set realtime priority: error %d", res);
    }
#endif

    USDR_LOG("USBX", USDR_LOG_INFO, "IO thread started");

    while (!dev->stop) {
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        res = libusb_handle_events_timeout(dev->ctx, &tv);
        // TODO: check res
    }

    USDR_LOG("USBX", USDR_LOG_INFO, "IO thread termitaed with result %d", res);
    return (void*)((intptr_t)res);
}


int libusb_generic_create_thread(libusb_generic_dev_t *dev)
{
    int res = pthread_create(&dev->io_thread, NULL, libusb_generic_io_thread, dev);
    if (res) {
        res = -res;
    }
    return res;
}

int libusb_generic_stop_thread(libusb_generic_dev_t *dev)
{
    dev->stop = true;
    int res = pthread_join(dev->io_thread, NULL);
    if (res) {
        res = -res;
    }
    return res;
}


int buffers_init(struct buffers* rb, unsigned max, unsigned zerosemval, bool has_event)
{
    rb->rqueuebuf_ptr = NULL;

    rb->allocsz = 0;
    rb->allocsz_rounded = 0;
    rb->buf_max = 0;
    rb->buf_available = 0;

    rb->bufno_cons = 0;
    rb->bufno_prod = 0;
    rb->bd = NULL;

    rb->buf_available = rb->buf_max = max;

    if (sem_init(&rb->buf_ready, 0, zerosemval)) {
        return -errno;
    }

    if (has_event) {
        rb->fd_event = fdevent_create(zerosemval);
        if (rb->fd_event < 0) {
            int err = -errno;
            USDR_LOG("USBX", USDR_LOG_ERROR, "Unable to create eventfd! err=%d\n", err);
            return err;
        }
    } else {
        rb->fd_event = -1;
    }

    rb->auto_restart = false;
    rb->stop = false;
    rb->transfers_count = 0;

    rb->on_buffer_param = NULL;
    rb->on_buffer = NULL;
    return 0;
}

void buffers_deinit(struct buffers* rb)
{
    int res;

    rb->stop = true;
    for (unsigned i = 0; i < rb->transfers_count; i++) {
        res = libusb_to_errno(libusb_cancel_transfer(rb->transfers[i]));
        if (res && res != -ENXIO) {
            USDR_LOG("USBX", USDR_LOG_WARNING, "libusb_cancel_transfer(%d/%d) error, res=%d\n",
                     i, rb->transfers_count, res);
        }
    }

    // TODO Add synchronization to get all outstanging endpoints
    usleep(10000);

    sem_destroy(&rb->buf_ready);
    free(rb->rqueuebuf_ptr);
    free(rb->bd);
    if (rb->fd_event >= 0)
        fdevent_destroy(rb->fd_event);
    rb->fd_event = -101;

    rb->rqueuebuf_ptr = NULL;
    rb->bd = NULL;
}

int buffers_realloc(struct buffers* rb, unsigned allocsz)
{
    int res;
    unsigned i;

    free(rb->rqueuebuf_ptr);
    free(rb->bd);
    rb->allocsz = allocsz;

    // Round up to maximum transfer in Bulk and reserve two more transfer in the case
    rb->allocsz_rounded = (allocsz + 4095) & (~4095u);

    rb->bd = (struct buffer_discriptor *)malloc(sizeof(struct buffer_discriptor) * (rb->buf_max + 1));

    res = posix_memalign((void**)&rb->rqueuebuf_ptr, 4096,
                         rb->allocsz_rounded * (rb->buf_max + 1));
    if (res != 0)
        return -res;

    for (i = 0; i <= rb->buf_max; i++) {
        rb->bd[i].b = rb;
        rb->bd[i].bno = i;
        rb->bd[i].buffer_sz = 0;
    }

    USDR_LOG("USBX", USDR_LOG_ERROR, "RX buffer configured to %d bytes for %d original\n",
             rb->allocsz_rounded, allocsz);

    rb->bufno_prod = 0;
    rb->bufno_cons = 0;
    return 0;
}

void buffers_reset(struct buffers* rb)
{
    rb->bufno_cons = 0;
    rb->bufno_prod = 0;
    rb->buf_available = rb->buf_max;
    sem_destroy(&rb->buf_ready);
    sem_init(&rb->buf_ready, 0, 0);

    if (rb->fd_event > 0)
        fdevent_get(rb->fd_event, NULL);
}

// Ready buffer to or from IO
unsigned buffers_produce(struct buffers *prxb)
{
    return (prxb->bufno_prod++) & (prxb->buf_max - 1);
}

// Alloc buffer to be filled
unsigned buffers_consume(struct buffers *prxb)
{
    return (prxb->bufno_cons++) & (prxb->buf_max - 1);
}

int buffers_ready_wait(struct buffers *rxb, int64_t timeout_us)
{
    int res;
    if (rxb->fd_event >= 0) {
        res = fdevent_get(rxb->fd_event, NULL);
    } else {
        res = sem_wait_ex(&rxb->buf_ready, timeout_us * 1000);
    }
    return res;
}

int buffers_ready_post(struct buffers *rxb)
{
    int res;
    if (rxb->fd_event >= 0) {
        res = fdevent_post(rxb->fd_event, 1);
    } else {
        res = sem_post(&rxb->buf_ready);
    }
    return res;
}

unsigned buffers_available_get(struct buffers *prxb)
{
    uint32_t data;
    do {
        data = __atomic_load_n(&prxb->buf_available, __ATOMIC_SEQ_CST);
    } while ((data != 0) && !__atomic_compare_exchange_n(&prxb->buf_available, &data, data - 1, true, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));

    // Returns buf_available before DEC or 0 if it's not possible
    return data;
}

unsigned buffers_available_post(struct buffers *prxb)
{
    return __atomic_fetch_add(&prxb->buf_available, 1, __ATOMIC_SEQ_CST);
}



unsigned _buffers_prod_get_nolock(struct buffers *prxb)
{
    if (buffers_available_get(prxb) == 0) {
        //No more buffers are available, use dummy
        return prxb->buf_max;
    } else {
        //return (prxb->bufno_prod++) & (prxb->buf_max - 1);
        return buffers_produce(prxb);
    }
}

int buffers_usb_transfer_post(struct buffers *prxb, unsigned buffer_idx, unsigned length,
                              unsigned transfer_idx)
{
    int res;
    prxb->transfers[transfer_idx]->buffer = prxb->rqueuebuf_ptr + buffer_idx * prxb->allocsz_rounded;
    prxb->transfers[transfer_idx]->length = length; //prxb->allocsz_rounded;
    prxb->transfers[transfer_idx]->user_data = &prxb->bd[buffer_idx];

    res = libusb_to_errno(libusb_submit_transfer(prxb->transfers[transfer_idx]));
    if (res) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "FAILED to post %s_STRM[%d] buf %d error %d\n",
                 (prxb->transfers[transfer_idx]->endpoint & LIBUSB_ENDPOINT_IN) ? "IN" : "OUT",
                 transfer_idx, buffer_idx, res);
    } else {
        USDR_LOG("USBX", USDR_LOG_DEBUG, "posted %s_STRM[%d]/%d (prod=%d cons=%d)\n",
                 (prxb->transfers[transfer_idx]->endpoint & LIBUSB_ENDPOINT_IN) ? "IN" : "OUT",
                 transfer_idx, buffer_idx, prxb->bufno_prod, prxb->bufno_cons);
    }
    return res;
}

void LIBUSB_CALL libusb_transfer_buffers_cb(struct libusb_transfer *transfer)
{
    struct buffer_discriptor *rxbd = (struct buffer_discriptor *)transfer->user_data;
    struct buffers *rxb = rxbd->b;
    int res;
    int idx;
    uint8_t *mptr;
    const char* tr_type = (transfer->endpoint & LIBUSB_ENDPOINT_IN) ? "IN" : "OUT";
    for (idx = 0; idx < rxb->transfers_count; idx++) {
        if (transfer == rxb->transfers[idx])
            break;
    }
    assert(idx < rxb->transfers_count);

    USDR_LOG("USBX", USDR_LOG_DEBUG, "%s_STRM[%d] transfer %d => %d / %d\n", tr_type,
             idx, transfer->status, transfer->actual_length, transfer->length);

    if (rxb->stop) {
        // TODO: Syncronize EPs
        return;
    }

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "%s_STRM transfer completed with %d status, actual=%d!\n",
                 tr_type, transfer->status, transfer->actual_length);

        if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT ||
                transfer->status == LIBUSB_TRANSFER_ERROR) {
            goto restart;
        }
        return;
    }

    mptr = rxb->rqueuebuf_ptr + rxb->allocsz_rounded * rxbd->bno;
    if ((transfer->actual_length < 128) && (transfer->endpoint & LIBUSB_ENDPOINT_IN)) {
        USDR_LOG("USBX", USDR_LOG_ERROR, "%s_STRM bogus length: %d [ %02x %02x %02x %02x ]\n",
                 tr_type, transfer->actual_length,
                 mptr[0], mptr[1], mptr[2], mptr[3]);

        goto restart;
    }

    rxbd->buffer_sz = transfer->actual_length;
    if (rxb->on_buffer) {
        rxb->on_buffer(rxb->on_buffer_param, rxbd);
    } else if (rxbd->bno < rxb->buf_max) {
        buffers_ready_post(rxb);
    }

    // Resubmit
restart:
    if (rxb->auto_restart && (transfer->endpoint & LIBUSB_ENDPOINT_IN)) {
        res = buffers_usb_transfer_post(rxb,
                                        _buffers_prod_get_nolock(rxb),
                                        rxb->allocsz_rounded,
                                        idx);
        if (res) {
            USDR_LOG("USBX", USDR_LOG_ERROR, "IN_STRM[%d] transfer resumbit failed, error %d!\n",
                     idx, res);
        }
    }
}


int buffers_usb_init(libusb_generic_dev_t* gdev, struct buffers *prxb,
                     unsigned max_reqs, unsigned max_buffs, unsigned max_blocksize,
                     unsigned endpoint, bool eventfd_ntfy)
{
    bool usb_in = (endpoint & LIBUSB_ENDPOINT_IN) ? true : false;
    int res = 0;
    if (max_reqs > BUFFERS_MAX_TRANS) {
        max_reqs = BUFFERS_MAX_TRANS;
    }

    res = res ? res : buffers_init(prxb, max_buffs, usb_in ? 0 : max_buffs, eventfd_ntfy);
    res = res ? res : buffers_realloc(prxb, max_blocksize);
    res = res ? res : libusb_generic_prepare_transfer(gdev, NULL, endpoint,
                                                      LIBUSB_TRANSFER_TYPE_BULK,
                                                      prxb->transfers,
                                                      prxb->rqueuebuf_ptr,
                                                      prxb->allocsz_rounded,
                                                      max_reqs,
                                                      &libusb_transfer_buffers_cb);
    if (res) {
        return res;
    }

    for (unsigned j = 0; j < max_reqs; j++) {
        prxb->transfers[j]->user_data = &prxb->bd[j];
    }
    prxb->transfers_count = max_reqs;

    USDR_LOG("USBX", USDR_LOG_INFO, "%s_STRM endpoint %02x configured: %d requests, %d x %d buffers\n",
             usb_in ? "IN" : "OUT", endpoint, max_reqs, max_buffs, prxb->allocsz_rounded);
    return 0;
}

int buffers_usb_free(struct buffers *prxb)
{
    buffers_deinit(prxb);

    for (unsigned j = 0; j < prxb->transfers_count; j++) {
        libusb_free_transfer(prxb->transfers[j]);

        prxb->transfers[j] = NULL;
    }

    free(prxb->rqueuebuf_ptr);
    prxb->rqueuebuf_ptr = NULL;

    free(prxb->bd);
    prxb->bd = NULL;

    prxb->transfers_count = 0;
    return 0;
}

// Helpers

int sem_wait_ex(sem_t *s, int64_t timeout_ns)
{
    int res;
    if (timeout_ns > 0) {
        struct timespec ts;
        unsigned secs = timeout_ns / (1000 * 1000 * 1000);

        res = clock_gettime(CLOCK_REALTIME, &ts);
        if (res)
            return -EFAULT;

        ts.tv_sec += secs;
        timeout_ns -= (int64_t)1000 * 1000 * 1000 * secs;

        ts.tv_nsec += timeout_ns * 1000;
        while (ts.tv_nsec > 1000 * 1000 * 1000) {
            ts.tv_nsec -= 1000 * 1000 * 1000;
            ts.tv_sec++;
        }

        res = sem_timedwait(s, &ts);
    } else if (timeout_ns < 0) {
        res = sem_wait(s);
    } else {
        res = sem_trywait(s);
    }
    if (res) {
        // sem_* function on error returns -1, get proper error
        res = -errno;
    }
    return res;
}
