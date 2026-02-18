#include "usdr_port.h"

#ifdef _WIN32
void __attribute__ ((constructor(220))) setup_winsocks(void) {
    WSADATA wsaData;
    int iResult;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        _exit(2);
    }
}

library_hdl_t usdr_lib_load(const char* s)
{
    return LoadLibraryA(s);
}

void usdr_lib_close(library_hdl_t h)
{
    CloseHandle(h);
}

void* usdr_lib_sym(library_hdl_t h, const char* proc)
{
    return GetProcAddress(h, proc);
}


int vasprintf(char **strp, const char *fmt, va_list ap)
{
    int len = _vscprintf(fmt, ap);
    if (len == -1) {
        return -1;
    }

    size_t size = (size_t)len + 1;
    char *str = malloc(size);
    if (!str) {
        return -1;
    }

    int r = _vsprintf_s_l(str, len + 1, fmt, NULL, ap);
    if (r == -1) {
        free(str);
        return -1;
    }

    *strp = str;
    return r;
}

int asprintf(char **strp, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}


#else

library_hdl_t usdr_lib_load(const char* s)
{
    int mode = RTLD_LAZY | RTLD_GLOBAL;
#ifdef RTLD_DEEPBIND
    mode |= RTLD_DEEPBIND;
#endif
    return dlopen(s, mode);
}

void usdr_lib_close(library_hdl_t h)
{
    dlclose(h);
}

void* usdr_lib_sym(library_hdl_t h, const char* proc)
{
    return dlsym(h, proc);
}

#endif


#ifdef __APPLE__
#include <errno.h>
#include <time.h>

/**
 * A wrapper for sem_timedwait functionality using Mach semaphores.
 * @param sem The Mach semaphore_t to wait on.
 * @param abs_timeout The absolute wall-clock time to wait until.
 * @return 0 on success, -1 on error (with errno set).
 */
int mach_sem_timedwait(semaphore_t sem, const struct timespec *abs_timeout) {
    // 1. Get current wall-clock time
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    // 2. Calculate relative timeout in nanoseconds
    int64_t rel_ns = (abs_timeout->tv_sec - now.tv_sec) * 1000000000LL +
                     (abs_timeout->tv_nsec - now.tv_nsec);

    if (rel_ns <= 0) {
        // Time has already passed
        return (semaphore_wait_noblock(sem) == KERN_SUCCESS) ? 0 : (errno = ETIMEDOUT, -1);
    }

    // 3. Convert relative nanoseconds to Mach ticks (deadline)
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t rel_ticks = rel_ns * timebase.denom / timebase.numer;
    uint64_t deadline = mach_absolute_time() + rel_ticks;

    // 4. Perform the wait
    kern_return_t kr = semaphore_wait_deadline(sem, deadline);

    if (kr == KERN_SUCCESS) {
        return 0;
    } else if (kr == KERN_OPERATION_TIMED_OUT) {
        errno = ETIMEDOUT;
        return -1;
    } else {
        errno = EINVAL;
        return -1;
    }
}

#endif


