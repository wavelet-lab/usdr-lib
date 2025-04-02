// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include "vbase.h"

#include <usdr_logging.h>

Suite * ring_buffer_suite(void);
Suite * trig_suite(void);
Suite * clockgen_suite(void);
Suite * lmk05318_solver_suite(void);

int main(int argc, char** argv)
{
    char buffer[100];
    int number_failed;
    SRunner *sr;

    cpu_vcap_str(buffer, sizeof(buffer),
                 cpu_vcap_obtain((argc > 2) ? (CVF_LIMIT_VCPU | atoi(argv[2])) : 0));

    fprintf(stderr, "Running with %s CPU features\n", buffer);
    usdrlog_setlevel(NULL, (argc > 1) ? USDR_LOG_TRACE : USDR_LOG_INFO);
    usdrlog_enablecolorize(NULL);
#if 0
    sr = srunner_create(ring_buffer_suite());
    srunner_add_suite(sr, trig_suite());
    srunner_add_suite(sr, clockgen_suite());
    srunner_add_suite(sr, lmk05318_solver_suite());
#else
    sr = srunner_create(lmk05318_solver_suite());
#endif

    srunner_set_fork_status (sr, CK_NOFORK);

    srunner_run_all(sr, (argc > 1) ? CK_VERBOSE : CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
