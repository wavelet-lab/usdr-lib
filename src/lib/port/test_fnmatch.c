#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portable_fnmatch.h"

/* If system fnmatch exists, include it for reference comparisons */
#if defined(__unix__) || defined(__APPLE__)
#include <fnmatch.h>
#define HAVE_SYSTEM_FNMATCH 1
#else
#define HAVE_SYSTEM_FNMATCH 0
#endif

/* Compare portable to system fnmatch when available.
 * You can force-disable with -DNO_SYSTEM_COMPARE.
 */
#if HAVE_SYSTEM_FNMATCH && !defined(NO_SYSTEM_COMPARE)
#define DO_SYSTEM_COMPARE 1
#else
#define DO_SYSTEM_COMPARE 0
#endif

typedef struct {
    const char *pat;
    const char *str;
    int flags;
    int expect;      /* 0 or FNM_NOMATCH */
    const char *desc;
} tc_t;

static const tc_t cases[] = {
    {"", "", 0, 0, "empty matches empty"},
    {"", "a", 0, FNM_NOMATCH, "empty does not match non-empty"},
    {"a", "a", 0, 0, "literal"},
    {"abc", "abc", 0, 0, "literal multi"},
    {"abc", "ab", 0, FNM_NOMATCH, "literal length"},

    {"a?c", "abc", 0, 0, "? wildcard"},
    {"a?c", "ac", 0, FNM_NOMATCH, "? requires one char"},

    {"a*c", "abbbc", 0, 0, "* wildcard"},
    {"a*c", "ac", 0, 0, "* can be empty"},
    {"*", "anything", 0, 0, "* matches all"},

    {"a\\*c", "a*c", 0, 0, "escaped *"},
    {"a\\?c", "a?c", 0, 0, "escaped ?"},
    {"a\\*c", "abbbc", 0, FNM_NOMATCH, "escaped * is literal"},
    {"a\\*c", "a*c", FNM_NOESCAPE, FNM_NOMATCH, "noescape treats backslash literally"},

    {"[ab]", "a", 0, 0, "bracket set"},
    {"[ab]", "c", 0, FNM_NOMATCH, "bracket set no"},
    {"[a-c]", "b", 0, 0, "bracket range"},
    {"[!a-c]", "z", 0, 0, "bracket negate"},
    {"[!a-c]", "b", 0, FNM_NOMATCH, "bracket negate no"},
    {"[]]", "]", 0, 0, "']' first in class"},
    {"[-a]", "-", 0, 0, "'-' literal at start"},
    {"[a-]", "-", 0, 0, "'-' literal at end"},

    {"[[:digit:]]", "5", 0, 0, "posix class digit"},
    {"[[:alpha:]]", "Z", 0, 0, "posix class alpha"},
    {"[[:xdigit:]]", "f", 0, 0, "posix class xdigit"},
    {"[[:digit:]]", "x", 0, FNM_NOMATCH, "posix class digit no"},

    {"ABC", "abc", FNM_CASEFOLD, 0, "casefold literal"},
    {"[A-C]", "b", FNM_CASEFOLD, 0, "casefold in range"},

    {"*", ".bashrc", FNM_PERIOD, FNM_NOMATCH, "period blocks leading dot"},
    {".*", ".bashrc", FNM_PERIOD, 0, "period explicit dot"},
    {"?", ".x", FNM_PERIOD, FNM_NOMATCH, "period blocks leading dot with ?"},

    {"a/*", "a/.b", FNM_PATHNAME | FNM_PERIOD, FNM_NOMATCH, "period blocks dot after /"},
    {"a/.*", "a/.b", FNM_PATHNAME | FNM_PERIOD, 0, "period explicit dot after /"},

    {"a*b", "a/x/b", FNM_PATHNAME, FNM_NOMATCH, "pathname: * cannot cross /"},
    {"a*b", "a/x/b", 0, 0, "no pathname: * can cross /"},
    {"a?b", "a/b", FNM_PATHNAME, FNM_NOMATCH, "pathname: ? cannot match /"},
    {"a[/]b", "a/b", FNM_PATHNAME, FNM_NOMATCH, "pathname: bracket cannot match /"},

    {"abc", "abc/def", FNM_PATHNAME | FNM_LEADING_DIR, 0, "leading_dir: literal prefix"},
    {"a*", "abcd/ef", FNM_PATHNAME | FNM_LEADING_DIR, 0, "leading_dir: star prefix"},
    {"a*", "abcd/ef", FNM_PATHNAME, FNM_NOMATCH, "no leading_dir: needs full match"},

    /* WINPATH extension */
    {"a/b", "a\\b", FNM_PATHNAME | FNM_WINPATH, 0, "winpath: / matches \\\\ in string"},
    {"a/*", "a\\b", FNM_PATHNAME | FNM_WINPATH, 0, "winpath: separator then component"},
    {"a/*", "a\\b\\c", FNM_PATHNAME | FNM_WINPATH, FNM_NOMATCH, "winpath: * cannot cross \\\\ separator"},

    /* invalid bracket treated literally */
    {"[", "[", 0, 0, "invalid bracket treated as literal"},
    };

static const char *flags_to_str(int f, char *buf, size_t n) {
    buf[0] = 0;
    int first = 1;
#define ADD(name, val) do { if (f & (val)) { if (!first) strncat(buf, "|", n-1); strncat(buf, name, n-1); first = 0; } } while(0)
    ADD("NOESCAPE", FNM_NOESCAPE);
    ADD("PATHNAME", FNM_PATHNAME);
    ADD("PERIOD", FNM_PERIOD);
    ADD("LEADING_DIR", FNM_LEADING_DIR);
    ADD("CASEFOLD", FNM_CASEFOLD);
    ADD("WINPATH", FNM_WINPATH);
#undef ADD
    if (first) strncpy(buf, "0", n);
    buf[n-1] = 0;
    return buf;
}

static void run_fixed_tests(void) {
    int fails = 0;
    int sys_mismatch = 0;

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        const tc_t *t = &cases[i];
        int r = portable_fnmatch(t->pat, t->str, t->flags);
        int ok = (r == t->expect);

        if (!ok) {
            char fb[128];
            fprintf(stderr, "FAIL[%zu]: %s\n  pat='%s' str='%s' flags=%s\n  got=%d expect=%d\n",
                    i, t->desc, t->pat, t->str, flags_to_str(t->flags, fb, sizeof(fb)), r, t->expect);
            fails++;
        }

#if DO_SYSTEM_COMPARE
        {
            /* System fnmatch doesn't know WINPATH; mask it out for reference comparison */
            int sys_flags = t->flags & ~FNM_WINPATH;

            int sr = fnmatch(t->pat, t->str, sys_flags);
            sr = (sr == 0) ? 0 : FNM_NOMATCH;

            int pr = (r == 0) ? 0 : FNM_NOMATCH;

            if (sr != pr) {
                char fb[128];
                fprintf(stderr, "SYS MISMATCH[%zu]: %s\n  pat='%s' str='%s' flags=%s\n  portable=%d system=%d\n",
                        i, t->desc, t->pat, t->str, flags_to_str(t->flags, fb, sizeof(fb)), pr, sr);
                sys_mismatch++;
            }
        }
#endif
    }

    if (fails == 0)
        printf("Fixed tests: PASS (%zu cases)\n", sizeof(cases)/sizeof(cases[0]));
    else {
        printf("Fixed tests: FAIL (%d/%zu failed)\n", fails, sizeof(cases)/sizeof(cases[0]));
        exit(1);
    }

#if DO_SYSTEM_COMPARE
    if (sys_mismatch == 0)
        printf("System compare: OK (no mismatches on comparable cases)\n");
    else {
        printf("System compare: %d mismatches (see stderr)\n", sys_mismatch);
        exit(2);
    }
#endif
}

/* Simple fuzz: generate random patterns/strings (restricted alphabet) and compare to system fnmatch.
   Only enabled when system compare is available. */
static unsigned rng_u32(unsigned *st) {
    *st = (*st * 1103515245u + 12345u);
    return *st;
}

static void rand_str(unsigned *st, char *out, size_t maxlen, int want_pattern) {
    static const char alpha[] = "abcXYZ012-._/";
    static const char meta[]  = "*?[]!\\";
    size_t len = (rng_u32(st) % (unsigned)maxlen);

    for (size_t i = 0; i < len; i++) {
        unsigned r = rng_u32(st);
        if (want_pattern && (r % 6 == 0))
            out[i] = meta[r % (sizeof(meta)-1)];
        else
            out[i] = alpha[r % (sizeof(alpha)-1)];
    }
    out[len] = '\0';

    if (want_pattern && (rng_u32(st) % 5 == 0))
        strcat(out, "*");
}

static void run_fuzz(int iters) {
#if DO_SYSTEM_COMPARE
    unsigned st = 0xC0FFEEu;
    int mism = 0;

    for (int i = 0; i < iters; i++) {
        char pat[64], str[64];
        rand_str(&st, pat, 20, 1);
        rand_str(&st, str, 20, 0);

        int flags = 0;
        if (rng_u32(&st) & 1) flags |= FNM_NOESCAPE;
        if (rng_u32(&st) & 1) flags |= FNM_PATHNAME;
        if (rng_u32(&st) & 1) flags |= FNM_PERIOD;

        int pr = portable_fnmatch(pat, str, flags);
        int sr = fnmatch(pat, str, flags);

        sr = (sr == 0) ? 0 : FNM_NOMATCH;
        pr = (pr == 0) ? 0 : FNM_NOMATCH;

        if (pr != sr) {
            char fb[128];
            fprintf(stderr, "FUZZ MISMATCH[%d]: pat='%s' str='%s' flags=%s portable=%d system=%d\n",
                    i, pat, str, flags_to_str(flags, fb, sizeof(fb)), pr, sr);
            mism++;
            if (mism > 50) break;
        }
    }

    if (mism == 0)
        printf("Fuzz: OK (%d iterations)\n", iters);
    else {
        printf("Fuzz: %d mismatches (see stderr)\n", mism);
        exit(3);
    }
#else
    (void)iters;
    printf("Fuzz: skipped (no system fnmatch available)\n");
#endif
}

int main(int argc, char **argv) {
    run_fixed_tests();

    if (argc == 3 && strcmp(argv[1], "--fuzz") == 0) {
        int iters = atoi(argv[2]);
        if (iters <= 0) iters = 10000;
        run_fuzz(iters);
    }
    return 0;
}
