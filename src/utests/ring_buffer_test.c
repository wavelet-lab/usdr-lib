// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdlib.h>

#include "ring_buffer.h"

START_TEST(test_create) {
    struct ring_buffer *v = ring_buffer_create(1, 1);
    ck_assert_ptr_ne(v, NULL);

    ring_buffer_destroy(v);
}
END_TEST

START_TEST(test_produce) {
    unsigned pidx;
    struct ring_buffer *v = ring_buffer_create(4, 1);
    ck_assert_ptr_ne(v, NULL);

    for (unsigned i = 0; i < 4; i++) {
        pidx = ring_buffer_pwait(v, 0);
        ck_assert_int_eq(pidx, i);
    }

    pidx = ring_buffer_pwait(v, 0);
    ck_assert_int_eq(pidx, IDX_TIMEDOUT);

    ring_buffer_destroy(v);
}
END_TEST

START_TEST(test_produce_consume) {
    unsigned pidx, cidx;
    char *b;
    struct ring_buffer *v = ring_buffer_create(4, 1);
    ck_assert_ptr_ne(v, NULL);

    for (unsigned i = 0; i < 4; i++) {
        pidx = ring_buffer_pwait(v, 0);
        ck_assert_int_eq(pidx, i);

        b = ring_buffer_at(v, pidx);
        *b = 32 + i;
        ring_buffer_ppost(v);
    }

    pidx = ring_buffer_pwait(v, 0);
    ck_assert_int_eq(pidx, IDX_TIMEDOUT);

    for (unsigned i = 0; i < 4; i++) {
        cidx = ring_buffer_cwait(v, 0);
        ck_assert_int_eq(cidx, i);

        b = ring_buffer_at(v, cidx);
        ck_assert_int_eq(*b, 32 + i);
        ring_buffer_cpost(v);
    }

    cidx = ring_buffer_cwait(v, 0);
    ck_assert_int_eq(cidx, IDX_TIMEDOUT);

    ring_buffer_destroy(v);
}
END_TEST

START_TEST(test_pc_1m) {
    unsigned pidx, cidx;
    unsigned *b;
    struct ring_buffer *v = ring_buffer_create(4, 4);
    ck_assert_ptr_ne(v, NULL);

    for (unsigned i = 0; i < 100000; i++) {
        pidx = ring_buffer_pwait(v, -1);
        ck_assert_int_eq(pidx, i);

        b = (unsigned *) ring_buffer_at(v, pidx);
        *b = 32 + i;
        ring_buffer_ppost(v);

        cidx = ring_buffer_cwait(v, -1);
        ck_assert_int_eq(cidx, i);

        b = (unsigned *) ring_buffer_at(v, cidx);
        ck_assert_int_eq(*b, 32 + i);
        ring_buffer_cpost(v);
    }

    ring_buffer_destroy(v);
}
END_TEST

Suite * ring_buffer_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("RingBuffer");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_create);
    tcase_add_test(tc_core, test_produce);
    tcase_add_test(tc_core, test_produce_consume);
    tcase_add_test(tc_core, test_pc_1m);
    suite_add_tcase(s, tc_core);
    return s;
}

