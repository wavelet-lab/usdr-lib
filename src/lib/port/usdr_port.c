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

