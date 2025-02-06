// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USDR_LOGGING_H
#define USDR_LOGGING_H

#include "usdr_port.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    USDR_LOG_ERROR,
    USDR_LOG_CRITICAL_WARNING,
    USDR_LOG_WARNING,
    USDR_LOG_INFO,
    USDR_LOG_NOTE,
    USDR_LOG_DEBUG,
    USDR_LOG_TRACE,
};

void usdrlog_out(unsigned loglevel,
                 const char* subsystem,
                 const char* function,
                 const char* file,
                 int line,
                 const char* fmt, ...)  __attribute__ ((format (printf, 6, 7)));

void usdrlog_vout(unsigned loglevel,
                  const char* subsystem,
                  const char* function,
                  const char* file,
                  int line,
                  const char* fmt,
                  va_list list)  __attribute__ ((format (printf, 6, 0)));

/**
 * @brief usdrlog_setlevel
 * @param subsystem Desired subsystem, @b NULL for default
 * @param loglevel
 */
void usdrlog_setlevel(const char* subsystem,
                      unsigned loglevel);

/**
 * @brief usdrlog_getlevel
 * @param subsystem Returns loglevel set to specified substem if exists or default otherwise
 */
unsigned usdrlog_getlevel(const char* subsystem);

// TODO
// void usdrlog_setoutfunc();

// Helper macros

#define USDR_LOG(system, level, ...) \
    do { \
        usdrlog_out((level), (system), __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

typedef int (*s_log_op_t)(uintptr_t param, unsigned sevirity, const char* log);

void usdrlog_disablecolorize(const char* subsystem);
void usdrlog_enablecolorize(const char* subsystem);
bool usdr_check_level(unsigned loglevel, const char* subsystem);
void usdrlog_set_log_op( s_log_op_t op );

#ifdef __cplusplus
}
#endif

#endif
