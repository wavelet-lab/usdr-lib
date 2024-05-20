// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <signal.h>
#ifndef WIN32
#include <sys/un.h>
#include <sys/socket.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include "dm_dev.h"
#include "dm_dev_impl.h"
#include "dm_debug.h"

static
int usdr_dif_process_cmd(struct usdr_debug_ctx* ctx, char *cmd, unsigned len,
                         char* reply, unsigned rlen)
{
    uint64_t oval = 0;
    int res = -EINVAL;
    int j;
    char *pptr[4] = {NULL,};
    char *str1 = NULL, *saveptr1 = NULL, *token = NULL;

    USDR_LOG("DBGS", USDR_LOG_DEBUG, "got cmd: %s\n", cmd);

    for (j = 0, str1 = cmd; j < 4; j++, str1 = NULL) {
        token = strtok_r(str1, ",", &saveptr1);
        if (token == NULL)
            break;

        pptr[j] = token;
    }

    // CMD,PATH,VALUE
    // CMD,PATH
    //dm_dev_entity_t entity;
    if (strcasecmp(pptr[0], "SETU64") == 0 && j == 3) {
//        entity = usdr_dmd_find_entity(ctx->dev, pptr[1]);
//        res = usdr_dme_check(entity);
//        if (res) {
//            USDR_LOG("DBGS", USDR_LOG_WARNING, "usdr_dme_check return %d\n", res);
//            goto incorrect_format;
//        }
//        res = usdr_dme_set_uint(ctx->dev, entity, strtoul(pptr[2], NULL, 16));
        res = usdr_dme_set_uint(ctx->dev, pptr[1], strtoul(pptr[2], NULL, 16));
    } else if (strcasecmp(pptr[0], "GETU64") == 0 && j == 2) {
//        entity = usdr_dmd_find_entity(ctx->dev, pptr[1]);
//        res = usdr_dme_check(entity);
//        if (res) {
//            USDR_LOG("DBGS", USDR_LOG_WARNING, "usdr_dme_check return %d\n", res);
//            goto incorrect_format;
//        }
//        res = usdr_dme_get_uint(ctx->dev, entity, &oval);
        res = usdr_dme_get_uint(ctx->dev, pptr[1], &oval);
    }

//incorrect_format:
    if (res == 0) {
        return snprintf(reply, rlen, "OK,%016" PRIx64 "\n", oval);
    } else {
        return snprintf(reply, rlen, "FAIL,%d\n", res);
    }
}

#if !defined(WIN32) && !defined(__EMSCRIPTEN__)
static void* usdr_dif_thread(void* param)
{
    int ret;
    struct sockaddr_un name;
    struct usdr_debug_ctx* ctx = (struct usdr_debug_ctx*)param;
    USDR_LOG("DBGS", USDR_LOG_INFO, "Starting USDR debug thread\n");
    sigset_t set;

    pthread_setname_np(pthread_self(), "debug_io");
    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    const char* fifoname = "usdr_debug_pipe";
    unlink(fifoname);

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        USDR_LOG("DBGS", USDR_LOG_INFO, "Unable to create socket: error %d\n", errno);
        return NULL;
    }

    memset(&name, 0, sizeof(struct sockaddr_un));
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, fifoname, sizeof(name.sun_path) - 1);

    ret = bind(sock, (const struct sockaddr *) &name,
               sizeof(struct sockaddr_un));
    if (ret == -1) {
        USDR_LOG("DBGS", USDR_LOG_INFO, "Unable to bind socket: error %d\n", errno);
        close(sock);
        return NULL;
    }

    ret = listen(sock, 20);
    if (ret == -1) {
        USDR_LOG("DBGS", USDR_LOG_INFO, "Unable to tisten to socket: error %d\n", errno);
        close(sock);
        return NULL;
    }

    ctx->fd = sock;
    for (;;) {
        ctx->clifd = -1;
        int data_socket = accept(sock, NULL, NULL);
        if (data_socket == -1) {
            USDR_LOG("DBGS", USDR_LOG_INFO, "Unable to accept socket: error %d\n", errno);
            close(sock);
            return NULL;
        }
        USDR_LOG("DBGS", USDR_LOG_INFO, "Connection established\n");
        ctx->clifd = data_socket;

        unsigned p = 0;
        char buffer[4096];
        char reply[4096];
        int replen;

        for (;;) {
            ssize_t res = read(data_socket, p + buffer, sizeof(buffer) - p);
            if (res <= 0) {
                USDR_LOG("DBGS", USDR_LOG_INFO, "Connection closed\n");
                goto connection_closed;
            }

            char* end = strchr(buffer, '\n');
            if (end == NULL) {
                p += res;
                if (p == sizeof(buffer)) {
                    USDR_LOG("DBGS", USDR_LOG_INFO, "Incorrect CMD!\n");
                    close(data_socket);
                    goto connection_closed;
                }
                continue;
            }

            // Terminate string
            *end = 0;

            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            replen = usdr_dif_process_cmd(ctx, buffer, end - buffer,
                                   reply, sizeof(reply));
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

            if (replen > 0) {
                write(data_socket, reply, replen);
            }

            ssize_t ech = end - (p + buffer);
            if (ech > res) {
                USDR_LOG("DBGS", USDR_LOG_INFO, "Moving extra %d/%d bytes\n",
                           (int)ech, (int)res);

                memmove(buffer, end + 1, ech - res);
                p = ech - res;
            }
        }

connection_closed:;
    }
    return NULL;
}
#endif


int usdr_dif_init(const char *params,
                  pdm_dev_t dev,
                  //void *obj,
                  struct usdr_debug_ctx** octx)
{
#if !defined(WIN32) && !defined(__EMSCRIPTEN__)

    int res;
    const char* fifoname = "usdr_debug_pipe";
    int fd = mkfifo(fifoname, 0666);
    if (fd < 0 && errno != EEXIST) {
        int err = -errno;
        USDR_LOG("DBGS", USDR_LOG_ERROR, "Unable to create FIFO file `%s`, error %d\n",
                   fifoname, err);
        return err;
    }

    struct usdr_debug_ctx* ctx = (struct usdr_debug_ctx*)malloc(sizeof(struct usdr_debug_ctx));
    if (!ctx)
        return -ENOMEM;

    //ctx->obj = obj;
    ctx->dev = dev;

    res = pthread_create(&ctx->debug_thread, NULL, usdr_dif_thread, ctx);
    if (res)
        goto failed_create_thread;

    *octx = ctx;
    return 0;

failed_create_thread:
    free(ctx);
    return res;
#else
    return 0;
#endif
}


int usdr_dif_free(struct usdr_debug_ctx* ctx)
{
#if !defined(WIN32) && !defined(__EMSCRIPTEN__)
    close(ctx->fd);

    pthread_cancel(ctx->debug_thread);
    pthread_join(ctx->debug_thread, NULL);

    if (ctx->clifd != -1)
        close(ctx->clifd);
    free(ctx);
#endif
    return 0;
}
