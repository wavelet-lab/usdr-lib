// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CLI_PARSE_H
#define CLI_PARSE_H

#include <getopt.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Define an application's option list once:
 *
 * #define APP_CLI_OPTIONS(X) \
 *     X(OPT_HELP, "help", 'h', no_argument, NULL, "Show this help") \
 *     X(OPT_SAMPLE_RATE, "sample-rate", 'r', required_argument, "SPS", \
 *       "Set the sample rate") \
 *     X(OPT_FOO, "foo", 256, optional_argument, "VALUE", \
 *       "A long-only option")
 *
 * CLI_DEFINE_OPTIONS(app, APP_CLI_OPTIONS)
 *
 * This defines enum app_option_id, app_long_options[] and
 * app_options_help[].
 */
struct cli_option_help {
    int id;
    const char* argument_name;
    const char* description;
};

#define CLI_OPTION_ENUM(id, long_name, value, argument, argument_name, description) \
    id = value,

#define CLI_OPTION_GETOPT(id, long_name, value, argument, argument_name, description) \
    { long_name, argument, NULL, id },

#define CLI_OPTION_HELP(id, long_name, value, argument, argument_name, description) \
    { id, argument_name, description },

#define CLI_DEFINE_OPTIONS(prefix, option_list)                                  \
    enum prefix##_option_id {                                                    \
        option_list(CLI_OPTION_ENUM)                                             \
    };                                                                           \
    static const struct option prefix##_long_options[] = {                       \
        option_list(CLI_OPTION_GETOPT)                                           \
        { NULL, 0, NULL, 0 }                                                     \
    };                                                                           \
    static const struct cli_option_help prefix##_options_help[] = {              \
        option_list(CLI_OPTION_HELP)                                             \
        { 0, NULL, NULL }                                                        \
    };

unsigned cli_parse_unsigned_or_exit(const char* option, const char* value);
int cli_parse_int_or_exit(const char* option, const char* value);
uint64_t cli_parse_u64_or_exit(const char* option, const char* value);
double cli_parse_double_or_exit(const char* option, const char* value);

/*
 * Parse decimal/scientific values with optional case-insensitive k, m or g
 * multipliers. A trailing "Hz" is also accepted, e.g. 4e6, 4M and 4MHz.
 * Lowercase 'm' intentionally means mega for convenient frequency input.
 */
double cli_parse_si_double_or_exit(const char* option, const char* value);
unsigned cli_parse_si_unsigned_or_exit(const char* option, const char* value);
uint64_t cli_parse_si_u64_or_exit(const char* option, const char* value);

int cli_build_short_options(const struct option* long_options,
                            char* short_options, size_t short_options_size);

void cli_print_usage(FILE* stream, const char* program, const char* synopsis,
                     const struct option* long_options,
                     const struct cli_option_help* options_help);
void cli_print_si_help(FILE* stream);

#endif
