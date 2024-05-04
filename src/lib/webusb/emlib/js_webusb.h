// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef JS_WEBUSB_H
#define JS_WEBUSB_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int init_lib(int fd, int vid, int pid);
int close_device(int fd);
int send_command(int fd, const char *cmd, size_t cmd_len, char *res, size_t res_len);
int send_debug_command(int fd, const char *cmd, size_t cmd_len, char *res, size_t res_len);

#ifdef __cplusplus
}
#endif
#endif
