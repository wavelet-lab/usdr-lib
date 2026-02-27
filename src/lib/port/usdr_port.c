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
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/task.h>

/**
 * Cross-platform semaphore implementation using Mach semaphores on macOS
 */

int usdr_sem_init(usdr_sem_t *sem, int pshared, unsigned int value) {
    (void)pshared;  // Mach semaphores don't support pshared
    kern_return_t kr = semaphore_create(mach_task_self(), sem, SYNC_POLICY_FIFO, (int)value);
    if (kr != KERN_SUCCESS) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int usdr_sem_destroy(usdr_sem_t *sem) {
    kern_return_t kr = semaphore_destroy(mach_task_self(), *sem);
    if (kr != KERN_SUCCESS) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int usdr_sem_post(usdr_sem_t *sem) {
    kern_return_t kr = semaphore_signal(*sem);
    if (kr != KERN_SUCCESS) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int usdr_sem_wait(usdr_sem_t *sem) {
    kern_return_t kr;
    do {
        kr = semaphore_wait(*sem);
    } while (kr == KERN_ABORTED);  // Handle spurious wakeups

    if (kr != KERN_SUCCESS) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int usdr_sem_trywait(usdr_sem_t *sem) {
    mach_timespec_t zero_timeout = {0, 0};
    kern_return_t kr = semaphore_timedwait(*sem, zero_timeout);

    if (kr == KERN_SUCCESS) {
        return 0;
    } else if (kr == KERN_OPERATION_TIMED_OUT) {
        errno = EAGAIN;
        return -1;
    } else {
        errno = EINVAL;
        return -1;
    }
}

int usdr_sem_timedwait(usdr_sem_t *sem, const struct timespec *abs_timeout) {
    // 1. Get current wall-clock time
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    // 2. Calculate relative timeout
    int64_t rel_sec = abs_timeout->tv_sec - now.tv_sec;
    int64_t rel_nsec = abs_timeout->tv_nsec - now.tv_nsec;

    // Normalize nanoseconds
    if (rel_nsec < 0) {
        rel_sec--;
        rel_nsec += 1000000000LL;
    }

    if (rel_sec < 0) {
        // Time has already passed, try a non-blocking wait
        return usdr_sem_trywait(sem);
    }

    // 3. Convert to mach_timespec_t (relative timeout)
    mach_timespec_t wait_time;
    wait_time.tv_sec = (unsigned int)rel_sec;
    wait_time.tv_nsec = (clock_res_t)rel_nsec;

    // 4. Perform the wait
    kern_return_t kr;
    do {
        kr = semaphore_timedwait(*sem, wait_time);
    } while (kr == KERN_ABORTED);  // Handle spurious wakeups

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

#else
/* Non-Apple: use POSIX semaphores */
#include <semaphore.h>
#include <errno.h>
#include <time.h>

int usdr_sem_init(usdr_sem_t *sem, int pshared, unsigned int value) {
    return sem_init(sem, pshared, value);
}

int usdr_sem_destroy(usdr_sem_t *sem) {
    return sem_destroy(sem);
}

int usdr_sem_post(usdr_sem_t *sem) {
    return sem_post(sem);
}

int usdr_sem_wait(usdr_sem_t *sem) {
    return sem_wait(sem);
}

int usdr_sem_trywait(usdr_sem_t *sem) {
    return sem_trywait(sem);
}

int usdr_sem_timedwait(usdr_sem_t *sem, const struct timespec *abs_timeout) {
    return sem_timedwait(sem, abs_timeout);
}

#endif

/**
 * Cross-platform thread naming implementation
 */
int usdr_set_thread_name(const char* name) {
#ifdef _WIN32
    // Windows: SetThreadDescription requires Windows 10 1607+
    // For older versions, we'd need to use the SEH exception trick
    // For now, just return success without doing anything
    (void)name;
    return 0;
#elif defined(__APPLE__)
    // macOS: pthread_setname_np takes only the name, sets current thread
    return pthread_setname_np(name);
#elif defined(__linux__)
    // Linux: pthread_setname_np takes thread and name
    return pthread_setname_np(pthread_self(), name);
#else
    // Unknown platform
    (void)name;
    return 0;
#endif
}

/**
 * sincosf implementation for macOS
 * Computes sine and cosine of x simultaneously
 * On Linux this is a GNU extension, but macOS doesn't provide it
 */
#ifdef __APPLE__
#include <math.h>

extern void __sincosf(float x, float *s, float *c);

void sincosf(float x, float *sin_val, float *cos_val) {

    // __sincosf exists starting macOS 10.9
    if (__builtin_available(macOS 10.9, *)) {
        __sincosf(x, s, c);
        return;
    }

    // macOS provides __sincosf_stret on some versions, but it's not reliable
    // Use the straightforward implementation
    *sin_val = sinf(x);
    *cos_val = cosf(x);
}
#endif


