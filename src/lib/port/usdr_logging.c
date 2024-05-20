// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "usdr_logging.h"
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static unsigned s_def_loglevel = USDR_LOG_ERROR;
static bool s_colorize = false;

void __attribute__ ((constructor(101))) setup_logging(void) {
    char *envlog = getenv("USDR_LOGLEVEL");
    if (envlog) {
        s_def_loglevel = atoi(envlog);
    }
}

#define s_logfile stderr

static int standard_log_op(uintptr_t param, unsigned sevirity, const char* log)
{
    fputs(log, s_logfile);
    return 0;
}

static s_log_op_t s_log_op = &standard_log_op;

void usdrlog_out(unsigned loglevel,
                 const char* subsystem,
                 const char* function,
                 const char* file,
                 int line,
                 const char* fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    usdrlog_vout(loglevel, subsystem, function, file, line, fmt, ap);
    va_end(ap);
}


static const char* s_term_name[] =
{
    "\033[0;31mERROR: ",
    "\033[0;32mCRIT:  ",
    "\033[0;33mWARN:  ",
    "\033[0;34mINFO:  ",
    "\033[0;35mNOTE: ",
    "\033[0;36mDEBUG:  ",
    "\033[0;37mTRACE: ",
    "\033[0m",
};

#define LAST_TERM_NAME (SIZEOF_ARRAY(s_term_name) - 1)

#ifdef __EMSCRIPTEN__
#define THREAD_SAFE
#else
#define THREAD_SAFE __thread
#endif

static
const struct tm* usdr_localtime(time_t now)
{
    const unsigned SECONDS_PER_MINUTE = 60;
    const unsigned MINUTES_PER_HOUR = 60;
    const time_t   SECONDS_PER_DAY = 60 * 60 * 24;

    static THREAD_SAFE time_t day_start;
    static THREAD_SAFE time_t day_end;
    static THREAD_SAFE struct tm day_tm;

    time_t timeofday;

    if (!(day_start <= now && now < day_end)) {
        // initialize new day if changed
        localtime_r(&now, &day_tm);
        day_tm.tm_hour = day_tm.tm_min = day_tm.tm_sec = 0;
        day_start = mktime(&day_tm);
        day_end = day_start + SECONDS_PER_DAY;
    }

    timeofday = now - day_start;
    day_tm.tm_sec = timeofday % SECONDS_PER_MINUTE;
    timeofday /= SECONDS_PER_MINUTE;
    day_tm.tm_min = timeofday % MINUTES_PER_HOUR;
    timeofday /= MINUTES_PER_HOUR;
    day_tm.tm_hour = (int)timeofday;
    return &day_tm;
}

bool usdr_check_level(unsigned loglevel, const char* subsystem)
{
    return loglevel <= s_def_loglevel;
}

#define MAX_LOG_LINE 8912

void usdrlog_vout(unsigned loglevel,
                  const char* subsystem,
                  const char* function,
                  const char* file,
                  int line,
                  const char* fmt,
                  va_list list)
{
    (void)file;
    char buf[MAX_LOG_LINE];
    size_t stsz;
    int sz;

    if (loglevel > USDR_LOG_TRACE)
        loglevel = USDR_LOG_TRACE;

    if (!usdr_check_level(loglevel, subsystem))
        return;

    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    const struct tm* stm = usdr_localtime(tp.tv_sec);
    int nsec = (int)tp.tv_nsec;

    stsz = strftime(buf, sizeof(buf), "%H:%M:%S.", stm);
    sz = snprintf(buf + stsz - 1, sizeof(buf) - stsz, ".%06d %s",
                  (int)(nsec/1000),
                  s_term_name[loglevel] + ((s_colorize > 0) ? 0 : 7));
    if (sz < 0) {
        buf[MAX_LOG_LINE - 1] = 0;
        goto out_truncated;
    }
    stsz += (size_t)sz;

    if (usdr_check_level(USDR_LOG_DEBUG, subsystem)) {
        sz = snprintf(buf + stsz - 1, sizeof(buf) - stsz, " %s:%d [%4.4s] ",
                      function, line,
                      subsystem);
    } else {
        sz = snprintf(buf + stsz - 1, sizeof(buf) - stsz, " [%4.4s] ",
                      subsystem);
    }
    if (sz < 0) {
        buf[MAX_LOG_LINE - 1] = 0;
        goto out_truncated;
    }
    stsz += (size_t)sz;
    sz = vsnprintf(buf + stsz - 1, sizeof(buf) - stsz, fmt, list);
    if (sz < 0) {
        buf[MAX_LOG_LINE - 1] = 0;
        goto out_truncated;
    }
    stsz += (size_t)sz;
    if (buf[stsz-2] != '\n') {
        buf[stsz-1] = '\n';
        buf[stsz] = 0;
        stsz++;
    }

    if (s_colorize) {
        sz = snprintf(buf + stsz - 1, sizeof(buf) - stsz, "%s", s_term_name[LAST_TERM_NAME]);
        if (sz < 0) {
            buf[MAX_LOG_LINE - 1] = 0;
        }
    }

out_truncated:
    s_log_op(0, loglevel, buf);
}

void usdrlog_setlevel(const char* subsystem,
                      unsigned loglevel)
{
    s_def_loglevel = loglevel;
}

unsigned usdrlog_getlevel(const char* subsystem)
{
    return s_def_loglevel;
}

void usdrlog_disablecolorize(const char* subsystem)
{
    s_colorize = false;
}

void usdrlog_enablecolorize(const char* subsystem)
{
    s_colorize = true;
}

void usdrlog_set_log_op( s_log_op_t op )
{
    s_log_op = op;
}
