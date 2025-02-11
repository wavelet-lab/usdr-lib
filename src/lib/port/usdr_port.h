// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USDR_PORT_H
#define USDR_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <endian.h>

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


#endif
