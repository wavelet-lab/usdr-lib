// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <check.h>
#include <stdlib.h>
#include "../common/clock_gen.h"

#define SIZEOF_ARRAY(x) ((sizeof(x))/(sizeof((x)[0])))

const vco_range_t vco_lmk05028[2] = {
    {
        .vcomin = 4800000000ul,
        .vcomax = 5400000000ul,
    },
    {
        .vcomin = 5500000000ul,
        .vcomax = 6200000000ul,
    },
};

const unsigned invalid_prescaler[] = {10, 12, ~0u};
const div_range_t prescaler = {
    .mindiv = 4,
    .maxdiv = 13,
    .step = 1,
    .count = 1,
    .pinvlid = &invalid_prescaler[0],
};

const div_range_t final = {
    .mindiv = 2,
    .maxdiv = 1u<<20,
    .step = 2,
    .pinvlid = NULL,
};



START_TEST(find_vco_test_1) {
    const div_range_t divs[] = {
            prescaler, final
    };

    const uint64_t freqs[] = {
            122880000,
            30720000,
            61440001,
    };
    unsigned out_divs[5];

    int res = find_best_vco(vco_lmk05028, SIZEOF_ARRAY(vco_lmk05028),
                            divs, SIZEOF_ARRAY(divs),
                            freqs, out_divs, SIZEOF_ARRAY(freqs));

    ck_assert_int_ge(res, 0);

}
END_TEST


Suite * clockgen_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("clockgen");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, find_vco_test_1);

    suite_add_tcase(s, tc_core);
    return s;
}
