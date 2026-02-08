// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USDR_PORT_H
#define USDR_PORT_H

#define _FILE_OFFSET_BITS 64

#ifdef _WIN32
#include <Winsock2.h>
#include <sec_api/stdio_s.h>
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#ifdef _WIN32
#define htobe32(x) htonl(x)
#define be32toh(x) ntohl(x)
#else
#include <endian.h>
#include <dlfcn.h>
#include <arpa/inet.h>

#include <stdio.h>
#endif
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "portable_fnmatch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_THREAD __thread

#define SIZEOF_ARRAY(x)  (sizeof(x) / sizeof(x[0]))

#ifdef __GNUC__
#  define UNUSED __attribute__((__unused__))
#  define CHECK_CONSTANT_EQ(a, b) _Static_assert((unsigned)(a) == (unsigned)(b), "Broken ABI")
#else
#  define UNUSED
#  define CHECK_CONSTANT_EQ(a, b)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#define SAFE_STRCPY(dst, src) ({strncpy((dst), (src), sizeof(dst) - 1); dst[SIZEOF_ARRAY(dst) - 1] = 0; })

#ifdef _WIN32
static inline int usdr_alignalloc(void **memptr, size_t alignment, size_t size) {
    *memptr = _aligned_malloc(size, alignment);
    return (*memptr) ? 0 : ENOMEM;
}
static inline void usdr_alignfree(void* ptr) {
    _aligned_free(ptr);
}

#define localtime_r(T,Tm) (localtime_s(Tm,T) ? NULL : Tm)

#define ENAVAIL ENOENT

static inline int gettid(void)
{
    return GetCurrentThreadId();
}

#else
static inline int usdr_alignalloc(void **memptr, size_t alignment, size_t size) {
    return posix_memalign(memptr, alignment, size);
}
static inline void usdr_alignfree(void* ptr) {
    free(ptr);
}
#endif

#ifdef _WIN32
typedef HANDLE library_hdl_t;
typedef off64_t off_long_t;
#define ftell_long ftello64
#else
typedef void* library_hdl_t;
typedef off_t off_long_t;
#define ftell_long ftello
#endif

library_hdl_t usdr_lib_load(const char* s);
void usdr_lib_close(library_hdl_t h);
void* usdr_lib_sym(library_hdl_t h, const char* proc);

#ifdef _WIN32
int vasprintf(char **strp, const char *fmt, va_list ap)  __attribute__ ((format (printf, 2, 0)));
int asprintf(char **strp, const char *fmt, ...)  __attribute__ ((format (printf, 2, 3)));

#endif

#define CACHE_SIZE  64


#ifdef __cplusplus
};
#endif

#endif
