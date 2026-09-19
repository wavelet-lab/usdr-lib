#include "portable_fnmatch.h"

#include <ctype.h>
#include <string.h>

static int is_sep(char c, int flags) {
    if (flags & FNM_WINPATH)
        return (c == '/' || c == '\\');
    return (c == '/');
}

static unsigned char fold_ch(unsigned char c, int flags) {
    if (flags & FNM_CASEFOLD)
        return (unsigned char)tolower(c);
    return c;
}

static int ch_eq(char a, char b, int flags) {
    return fold_ch((unsigned char)a, flags) == fold_ch((unsigned char)b, flags);
}

static int is_leading_period(const char *s, const char *s0, int flags) {
    if (!(flags & FNM_PERIOD))
        return 0;
    if (*s != '.')
        return 0;
    if (s == s0)
        return 1;
    if ((flags & FNM_PATHNAME) && is_sep(s[-1], flags))
        return 1;
    return 0;
}

static int match_posix_class(const char *name, char test) {
    unsigned char c = (unsigned char)test;
    if (strcmp(name, "alnum") == 0) return isalnum(c);
    if (strcmp(name, "alpha") == 0) return isalpha(c);
    if (strcmp(name, "blank") == 0) return (c == ' ' || c == '\t');
    if (strcmp(name, "cntrl") == 0) return iscntrl(c);
    if (strcmp(name, "digit") == 0) return isdigit(c);
    if (strcmp(name, "graph") == 0) return isgraph(c);
    if (strcmp(name, "lower") == 0) return islower(c);
    if (strcmp(name, "print") == 0) return isprint(c);
    if (strcmp(name, "punct") == 0) return ispunct(c);
    if (strcmp(name, "space") == 0) return isspace(c);
    if (strcmp(name, "upper") == 0) return isupper(c);
    if (strcmp(name, "xdigit") == 0) return isxdigit(c);
    return 0;
}

/*
 * Bracket expression matcher.
 * Returns:
 *   1 if matched
 *   0 if not matched
 *  -1 if invalid (no closing ']') -> caller can treat '[' literally
 * On success/nomatch, *newp is set to the position after the closing ']'.
 */
static int bracket_match(const char *p, char test, int flags, const char **newp) {
    const char *pp = p + 1;
    int negate = 0;
    int ok = 0;

    if (*pp == '\0') {
        if (newp) *newp = p;
        return -1;
    }

    if (*pp == '!' || *pp == '^') {
        negate = 1;
        pp++;
    }

    /* ']' allowed first in class */
    if (*pp == ']') {
        if (ch_eq(']', test, flags))
            ok = 1;
        pp++;
    }

    for (; *pp != '\0' && *pp != ']'; pp++) {
        /* POSIX character class: [[:digit:]] etc */
        if (pp[0] == '[' && pp[1] == ':' ) {
            const char *name_start = pp + 2;
            const char *end = strstr(name_start, ":]");
            if (end) {
                char name[32];
                size_t nlen = (size_t)(end - name_start);
                if (nlen < sizeof(name)) {
                    memcpy(name, name_start, nlen);
                    name[nlen] = '\0';
                    if (match_posix_class(name, test))
                        ok = 1;
                    pp = end + 1; /* loop will ++pp => char after ']' of :]' */
                    continue;
                }
            }
        }

        char c1 = *pp;

        if (c1 == '\\' && !(flags & FNM_NOESCAPE) && pp[1] != '\0') {
            pp++;
            c1 = *pp;
        }

        /* Range a-b */
        if (pp[1] == '-' && pp[2] != '\0' && pp[2] != ']') {
            const char *pp2 = pp + 2;
            char c2 = *pp2;

            if (c2 == '\\' && !(flags & FNM_NOESCAPE) && pp2[1] != '\0') {
                pp2++;
                c2 = *pp2;
            }

            unsigned char a = fold_ch((unsigned char)c1, flags);
            unsigned char b = fold_ch((unsigned char)c2, flags);
            unsigned char t = fold_ch((unsigned char)test, flags);
            if (a <= t && t <= b)
                ok = 1;

            pp = pp2; /* loop will ++pp -> next char after range end */
            continue;
        }

        if (ch_eq(c1, test, flags))
            ok = 1;
    }

    if (*pp != ']') {
        if (newp) *newp = p;
        return -1;
    }

    pp++; /* consume ']' */
    if (newp) *newp = pp;

    if (negate)
        ok = !ok;

    return ok;
}

static int match_here(const char *p, const char *s, const char *s0, int flags) {
    for (;;) {
        char pc = *p;

        switch (pc) {
        case '\0':
            if ((flags & FNM_LEADING_DIR) && is_sep(*s, flags))
                return 0;
            return (*s == '\0') ? 0 : FNM_NOMATCH;

        case '?':
            if (*s == '\0')
                return FNM_NOMATCH;
            if ((flags & FNM_PATHNAME) && is_sep(*s, flags))
                return FNM_NOMATCH;
            if (is_leading_period(s, s0, flags))
                return FNM_NOMATCH;
            p++; s++;
            continue;

        case '*': {
            if (is_leading_period(s, s0, flags))
                return FNM_NOMATCH;

            while (*p == '*')
                p++;

            if (*p == '\0') {
                if (flags & FNM_PATHNAME) {
                    if (flags & FNM_LEADING_DIR)
                        return 0;
                    /* must not consume a separator */
                    for (const char *t = s; *t; t++) {
                        if (is_sep(*t, flags))
                            return FNM_NOMATCH;
                    }
                    return 0;
                }
                return 0;
            }

            for (const char *ss = s;; ss++) {
                int r = match_here(p, ss, s0, flags);
                if (r == 0)
                    return 0;

                if (*ss == '\0')
                    break;
                if ((flags & FNM_PATHNAME) && is_sep(*ss, flags))
                    break;
            }
            return FNM_NOMATCH;
        }

        case '[': {
            const char *np = NULL;
            int br;

            if (*s == '\0')
                return FNM_NOMATCH;
            if ((flags & FNM_PATHNAME) && is_sep(*s, flags))
                return FNM_NOMATCH;
            if (is_leading_period(s, s0, flags))
                return FNM_NOMATCH;

            br = bracket_match(p, *s, flags, &np);
            if (br == -1) {
                if (!ch_eq('[', *s, flags))
                    return FNM_NOMATCH;
                p++; s++;
                continue;
            }
            if (br == 0)
                return FNM_NOMATCH;

            p = np;
            s++;
            continue;
        }

        case '\\':
            if (!(flags & FNM_NOESCAPE) && p[1] != '\0') {
                p++;
                pc = *p;
            }
            /* fallthrough */
        default:
            if (*s == '\0')
                return FNM_NOMATCH;

            /* If PATHNAME, separators are special and must match separators */
            if ((flags & FNM_PATHNAME) && is_sep(*s, flags)) {
                if (!is_sep(pc, flags))
                    return FNM_NOMATCH;
                /* Treat '/' and '\\' as equivalent when WINPATH is set */
                p++; s++;
                continue;
            }

            if (!ch_eq(pc, *s, flags))
                return FNM_NOMATCH;
            p++; s++;
            continue;
        }
    }
}

int portable_fnmatch(const char *pattern, const char *string, int flags) {
    if (!pattern || !string)
        return FNM_NOMATCH;
    return match_here(pattern, string, string, flags);
}

/*
 * Optional drop-in: if the platform lacks a system fnmatch(), export one.
 * This is mainly for Windows/MinGW.
 *
 * Define PORTABLE_FNMATCH_FORCE_EXPORT_FNMATCH to export fnmatch() even if
 * a system <fnmatch.h> was found.
 */
#if !PORTABLE_FNMATCH_HAVE_SYSTEM_HEADER || defined(PORTABLE_FNMATCH_FORCE_EXPORT_FNMATCH)
int fnmatch(const char *pattern, const char *string, int flags) {
    return portable_fnmatch(pattern, string, flags);
}
#endif

