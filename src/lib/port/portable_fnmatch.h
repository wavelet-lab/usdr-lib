#ifndef PORTABLE_FNMATCH_H
#define PORTABLE_FNMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * If a system <fnmatch.h> exists, include it so we inherit the platform's
 * flag bit assignments (they are NOT universal across libcs).
 *
 * On MinGW/Windows, this header typically does not exist, so we fall back
 * to defining the common POSIX bits.
 */
#if !defined(PORTABLE_FNMATCH_NO_SYSTEM_HEADERS)
#if defined(__has_include)
#if __has_include(<fnmatch.h>)
#include <fnmatch.h>
#define PORTABLE_FNMATCH_HAVE_SYSTEM_HEADER 1
#else
#define PORTABLE_FNMATCH_HAVE_SYSTEM_HEADER 0
#endif
#else
/* Conservative fallback: assume no system header */
#define PORTABLE_FNMATCH_HAVE_SYSTEM_HEADER 0
#endif
#else
#define PORTABLE_FNMATCH_HAVE_SYSTEM_HEADER 0
#endif

/* Return codes */
#ifndef FNM_NOMATCH
#define FNM_NOMATCH 1
#endif

/* POSIX flags (define only if the platform header didn't) */
#ifndef FNM_NOESCAPE
#define FNM_NOESCAPE 0x01
#endif
#ifndef FNM_PATHNAME
#define FNM_PATHNAME 0x02
#endif
#ifndef FNM_PERIOD
#define FNM_PERIOD 0x04
#endif

/* Common extensions (define if missing) */
#ifndef FNM_LEADING_DIR
#define FNM_LEADING_DIR 0x08
#endif
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0x10
#endif

/* Windows-friendly extension: treat '\\' as a path separator too */
#ifndef FNM_WINPATH
#define FNM_WINPATH 0x20
#endif

/* Preferred API: doesn't collide with system fnmatch() */
int portable_fnmatch(const char *pattern, const char *string, int flags);

#if !PORTABLE_FNMATCH_HAVE_SYSTEM_HEADER || defined(PORTABLE_FNMATCH_FORCE_EXPORT_FNMATCH)
int fnmatch(const char *pattern, const char *string, int flags);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PORTABLE_FNMATCH_H */

