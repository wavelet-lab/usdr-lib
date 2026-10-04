// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "cli_parse.h"

#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cli_invalid_value(const char* option, const char* value)
{
    fprintf(stderr, "Invalid value for --%s: '%s'\n", option, value);
    exit(EXIT_FAILURE);
}

unsigned cli_parse_unsigned_or_exit(const char* option, const char* value)
{
    char* end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &end, 0);
    if (errno || end == value || *end != '\0' || parsed > UINT_MAX)
        cli_invalid_value(option, value);

    return (unsigned)parsed;
}

uint64_t cli_parse_u64_or_exit(const char* option, const char* value)
{
    char* end;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(value, &end, 0);
    if (value[0] == '-' || errno || end == value || *end != '\0')
        cli_invalid_value(option, value);

    return (uint64_t)parsed;
}

double cli_parse_double_or_exit(const char* option, const char* value)
{
    char* end;
    double parsed;

    errno = 0;
    parsed = strtod(value, &end);
    if (errno || end == value || *end != '\0' || !isfinite(parsed))
        cli_invalid_value(option, value);

    return parsed;
}

static double cli_parse_si_value_or_exit(const char* option, const char* value)
{
    char* end;
    double parsed;
    double multiplier = 1.0;

    errno = 0;
    parsed = strtod(value, &end);
    if (errno || end == value || !isfinite(parsed))
        cli_invalid_value(option, value);

    if (*end != '\0') {
        switch (tolower((unsigned char)*end)) {
        case 'k':
            multiplier = 1e3;
            end++;
            break;
        case 'm':
            multiplier = 1e6;
            end++;
            break;
        case 'g':
            multiplier = 1e9;
            end++;
            break;
        default:
            break;
        }

        if (tolower((unsigned char)end[0]) == 'h' &&
            tolower((unsigned char)end[1]) == 'z')
            end += 2;
    }

    parsed *= multiplier;
    if (*end != '\0' || !isfinite(parsed))
        cli_invalid_value(option, value);

    return parsed;
}

double cli_parse_si_double_or_exit(const char* option, const char* value)
{
    return cli_parse_si_value_or_exit(option, value);
}

unsigned cli_parse_si_unsigned_or_exit(const char* option, const char* value)
{
    double parsed = cli_parse_si_value_or_exit(option, value);

    if (parsed < 0 || parsed > UINT_MAX || floor(parsed) != parsed)
        cli_invalid_value(option, value);

    return (unsigned)parsed;
}

uint64_t cli_parse_si_u64_or_exit(const char* option, const char* value)
{
    double parsed = cli_parse_si_value_or_exit(option, value);

    if (parsed < 0 || parsed >= 18446744073709551616.0 || floor(parsed) != parsed)
        cli_invalid_value(option, value);

    return (uint64_t)parsed;
}

int cli_build_short_options(const struct option* long_options,
                            char* short_options, size_t short_options_size)
{
    size_t length = 0;

    if (long_options == NULL || short_options == NULL || short_options_size == 0)
        return -EINVAL;

    short_options[0] = '\0';

    for (const struct option* option = long_options; option->name != NULL; option++) {
        if (option->flag != NULL || option->val <= 0 || option->val > UCHAR_MAX)
            continue;
        if (option->has_arg != no_argument &&
            option->has_arg != required_argument &&
            option->has_arg != optional_argument)
            return -EINVAL;

        char value = (char)option->val;
        if (strchr(short_options, value) != NULL)
            return -EEXIST;

        size_t suffix_length = option->has_arg == required_argument ? 1 :
                               option->has_arg == optional_argument ? 2 : 0;
        if (length + 1 + suffix_length >= short_options_size)
            return -ENOSPC;

        short_options[length++] = value;
        while (suffix_length-- > 0)
            short_options[length++] = ':';
        short_options[length] = '\0';
    }

    return 0;
}

void cli_print_usage(FILE* stream, const char* program, const char* synopsis,
                     const struct option* long_options,
                     const struct cli_option_help* options_help)
{
    if (stream == NULL || program == NULL ||
        long_options == NULL || options_help == NULL)
        return;

    fprintf(stream, "Usage: %s%s%s\nOptions:\n", program,
            synopsis && synopsis[0] ? " " : "", synopsis ? synopsis : "");

    for (const struct cli_option_help* help = options_help;
         help->id != 0; help++) {
        const struct option* option = long_options;
        while (option->name != NULL && option->val != help->id)
            option++;
        if (option->name == NULL)
            continue;

        char option_text[128];
        const char* argument_name = help->argument_name ?
                                    help->argument_name : "VALUE";
        int offset;

        if (option->val > 0 && option->val <= UCHAR_MAX &&
            isprint((unsigned char)option->val)) {
            offset = snprintf(option_text, sizeof(option_text), "-%c, --%s",
                              option->val, option->name);
        } else {
            offset = snprintf(option_text, sizeof(option_text), "    --%s",
                              option->name);
        }

        if (offset < 0)
            continue;
        if ((size_t)offset >= sizeof(option_text))
            offset = sizeof(option_text) - 1;

        if (option->has_arg == required_argument) {
            snprintf(option_text + offset, sizeof(option_text) - offset,
                     " <%s>", argument_name);
        } else if (option->has_arg == optional_argument) {
            snprintf(option_text + offset, sizeof(option_text) - offset,
                     "[=%s]", argument_name);
        }

        fprintf(stream, "  %-36s %s\n", option_text,
                help->description ? help->description : "");
    }
}

void cli_print_si_help(FILE* stream)
{
    if (stream == NULL)
        return;

    fprintf(stream,
            "\nNumeric frequency, bandwidth and rate values:\n"
            "  Scientific notation and case-insensitive k/m/g multipliers are supported.\n"
            "  Examples: 4e6, 4M, 4MHz, 100k, 2.4G, 2.4GHz.\n");
}
