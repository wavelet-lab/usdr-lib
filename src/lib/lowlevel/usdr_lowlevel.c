// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "usdr_lowlevel.h"
#include "usb_uram/usb_uram_generic.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

lowlevel_ops_t *lowlevel_get_ops(lldev_t dev)
{
    return dev->ops;
}

device_t* lowlevel_get_device(lldev_t dev)
{
    return dev->pdev;
}

const char* lowlevel_get_devname(lldev_t dev)
{
    const char* name;
    if (lowlevel_get_ops(dev)->generic_get(dev, LLGO_DEVICE_NAME, &name)) {
        return "[unknown]";
    }
    return name;
}

const uint8_t* lowlevel_get_uuid(lldev_t dev)
{
    const char* name;
    if (lowlevel_get_ops(dev)->generic_get(dev, LLGO_DEVICE_UUID, &name)) {
        return NULL;
    }
    return (const uint8_t*)name;
}


int lowlevel_info(UNUSED const char* driver,
                  UNUSED unsigned iparam,
                  UNUSED size_t osz,
                  UNUSED char* obuffer)
{
    return -EINVAL;
}

static unsigned s_driver_count = 0;
static const struct lowlevel_plugin* plugins[8];

const struct lowlevel_plugin* pcie_uram_register();
const struct lowlevel_plugin* verilator_wrap_register();
const struct lowlevel_plugin* usbft601_uram_register();

static
unsigned lowlevel_initialize_plugins()
{
    if (s_driver_count) {
        return s_driver_count;
    }

    //TODO driver loading
    plugins[s_driver_count++] = usbft601_uram_register();
    plugins[s_driver_count++] = usb_uram_register();
#if !defined(__EMSCRIPTEN__) && !defined(WVLT_WEBUSB_BUILD)
    plugins[s_driver_count++] = pcie_uram_register();
#endif

#ifdef ENABLE_VERILATOR
    plugins[s_driver_count++] = verilator_wrap_register();
#endif

    return s_driver_count;
}

int lowlevel_create(unsigned pcount, const char** devparam,
                    const char** devval, lldev_t* odev,
                    unsigned vidpid, void* webops, uintptr_t param)
{
    unsigned dcnt = lowlevel_initialize_plugins();
    unsigned i;
    int res = -ENODEV;
    for (i = 0; i < dcnt; i++) {
        res = plugins[i]->create(pcount, devparam, devval, odev, vidpid, webops, param);
        if (res == 0)
            return 0;
    }
    return res;
}

int lowlevel_discovery(unsigned pcount, const char** devparam, const char **devval,
                       unsigned maxbuf, char* buf)
{
    unsigned off = 0;
    int count = 0;

    for (unsigned i = 0; i < s_driver_count; i++) {
        int res = plugins[i]->discovery(pcount, devparam, devval, maxbuf - off, buf + off);
        if (res == -ENOMEM) {
            buf[off] = 0;
            return count;
        } else if (res <= 0) {
            continue;
        }

        count += res;
        off += strnlen(buf + off, maxbuf - off);
    }

    return count;
}

void lowlevel_ops_set_custom(lldev_t obj, lowlevel_ops_t* newops)
{
    obj->ops = newops;
}

static bool s_show_devname = false;

void usdrlog_ll_devname_en(bool show_devname)
{
    s_show_devname = show_devname;
}

void usdrlog_ll_out(lldev_t dev,
                    unsigned loglevel,
                    const char* subsystem,
                    const char* function,
                    const char* file,
                    int line,
                    const char* fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    usdrlog_ll_vout(dev, loglevel, subsystem, function, file, line, fmt, ap);
    va_end(ap);
}

void usdrlog_ll_vout(lldev_t dev,
                     unsigned loglevel,
                     const char* subsystem,
                     const char* function,
                     const char* file,
                     int line,
                     const char* fmt,
                     va_list list)
{
    if (s_show_devname) {
        char buf[8192];
        int sz = snprintf(buf, sizeof(buf), "%s: %s", lowlevel_get_devname(dev), fmt);
        if (sz < 0) {
            buf[8192 - 1] = 0;
        }

        return usdrlog_vout(loglevel, subsystem, function, file, line, buf, list);
    }

    return usdrlog_vout(loglevel, subsystem, function, file, line, fmt, list);
}


void __attribute__ ((constructor(110))) setup_lowlevel(void) {
    lowlevel_initialize_plugins();
}
