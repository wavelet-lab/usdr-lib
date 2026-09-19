// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USDR_PORT_H
#define USDR_PORT_H

#define _FILE_OFFSET_BITS 64

#ifdef _WIN32
#include <Winsock2.h>
#include <sec_api/stdio_s.h>
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
#elif defined(__APPLE__)
#include <machine/endian.h>
#include <libkern/OSByteOrder.h>
#include <arpa/inet.h>
#include <dlfcn.h>

#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/mach_time.h>

#include <stdio.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)


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

/**
 * Cross-platform semaphore abstraction
 * Uses Mach semaphores on macOS, POSIX semaphores elsewhere
 */
#ifdef __APPLE__
typedef semaphore_t usdr_sem_t;
#else
#include <semaphore.h>
typedef sem_t usdr_sem_t;
#endif

int usdr_sem_init(usdr_sem_t *sem, int pshared, unsigned int value);
int usdr_sem_destroy(usdr_sem_t *sem);
int usdr_sem_post(usdr_sem_t *sem);
int usdr_sem_wait(usdr_sem_t *sem);
int usdr_sem_trywait(usdr_sem_t *sem);
int usdr_sem_timedwait(usdr_sem_t *sem, const struct timespec *abs_timeout);

/**
 * Cross-platform thread naming
 * Sets the name of the current thread for debugging purposes
 * @param name Thread name (max 15 characters on Linux, 63 on macOS)
 * @return 0 on success, -1 on error
 */
int usdr_set_thread_name(const char* name);

/**
 * sincosf - compute sine and cosine simultaneously
 * Available natively on Linux (GNU extension), needs implementation on macOS/Windows
 */
#ifdef __APPLE__
#include <math.h>

// Use straightforward implementation on macOS
// Note: __sincosf is a private Apple symbol and may cause linker issues
static inline void sincosf(float x, float *sin_val, float *cos_val) {
    *sin_val = sinf(x);
    *cos_val = cosf(x);
}
#endif

#define CACHE_SIZE  64

#if defined(_WIN32) || defined(__APPLE__)
#define ENAVAIL ENOENT
#endif

#ifdef __cplusplus
};
#endif

#endif
