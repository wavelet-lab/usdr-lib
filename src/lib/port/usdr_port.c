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

