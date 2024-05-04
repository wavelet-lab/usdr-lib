// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "js_webusb.h"
#include "webusb.h"
#include <errno.h>
#include <emscripten.h>
#include <string.h>
#include <stdio.h>


EM_ASYNC_JS(int, write_ep1, (int fd, const char* data, size_t len), {
    return await write_ep1(fd, data, len);
});

EM_ASYNC_JS(int, read_ep1, (int fd, char* data, size_t len), {
    return await read_ep1(fd, data, len);
});

EM_ASYNC_JS(int, write_ep2, (int fd, const char* data, size_t len), {
    return await write_ep2(fd, data, len);
});

EM_ASYNC_JS(int, read_ep2, (int fd, char* data, size_t len), {
    return await read_ep2(fd, data, len);
});

EM_ASYNC_JS(void, write_log, (int fd, int sevirity, const char* str), {
    return write_log_js(fd, sevirity, str);
});

static
int write_raw(uintptr_t fd, uint8_t ep, unsigned bytes, const void *data)
{
    switch (ep & 0x7f) {
    case 1: return  write_ep1(fd, (const char*)data, bytes);
    case 2: return  write_ep2(fd, (const char*)data, bytes);
    }
    return -EINVAL;
}


static
int read_raw(uintptr_t fd, uint8_t ep, unsigned bytes, void *data)
{
    switch (ep & 0x7f) {
    case 1: return read_ep1(fd, (char*)data, bytes);
    case 2: return read_ep2(fd, (char*)data, bytes);
    }
    return -EINVAL;
}

static
int cblog(uintptr_t fd, unsigned sevirity, const char* log)
{
    int len = strlen(log);
    if (len > 0 && log[len - 1] == '\n')
        ((char*)log)[len - 1] = 0;

    write_log(fd, sevirity, log);
    return 0;
}

static
struct webusb_ops s_ops = {
    read_raw,
    write_raw,
    cblog,
};

#define MAX_DEV 8

static unsigned s_devlocked[MAX_DEV] = { 0 };
pdm_dev_t s_dev[MAX_DEV];


int init_lib(int fd, int vid, int pid)
{
    int res;
    unsigned vid_pid = (vid << 16) | pid;
    if (fd >= MAX_DEV || fd < 0)
        return 2;

    printf("Device fd = %d\n", fd);

    if (s_devlocked[fd])
        return 1;

    res = webusb_create(&s_ops, fd, 15, vid_pid, &s_dev[fd]);
    if (res)
    {
        printf("webusb_create(): result = %d\n", res);
        return res;
    }

    s_devlocked[fd] = 1;
    return 0;
}

int close_device(int fd)
{
    if (fd >= MAX_DEV || fd < 0)
        return 2;

    if (s_devlocked[fd]) {
        s_devlocked[fd] = 0;
        return webusb_destroy(s_dev[fd]);
    }
    return 0;
}

int send_command(int fd, const char* cmd, size_t cmd_len, char* res, size_t res_len)
{
    if (fd >= MAX_DEV || fd < 0)
        return 2;
    if (!s_devlocked[fd])
        return 1;

    return webusb_process_rpc(s_dev[fd], (char*)cmd, res, res_len);
}

int send_debug_command(int fd, const char* cmd, size_t cmd_len, char* res, size_t res_len)
{
    if (fd >= MAX_DEV || fd < 0)
        return 2;
    if (!s_devlocked[fd])
        return 1;

    return webusb_debug_rpc(s_dev[fd], (char*)cmd, res, res_len);
}
