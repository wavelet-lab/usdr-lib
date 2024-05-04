// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stddef.h>

#include "clock_gen.h"

#ifdef __SIZEOF_INT128__
// rewrite for no int128 type
typedef unsigned __int128 uint128_t;

pll_vals_t pll_solver(uint64_t vco, uint32_t ref, uint64_t den)
{
    // fVCO = fREF × (INT + NUM/ DEN)
    // F-RI = RN/DEN
    pll_vals_t o;
    uint64_t fi;

    o.nint = vco / ref;
    fi = (uint64_t)ref * o.nint;
    o.nfrac = (((uint128_t)(vco - fi)) * den) / ref;
    return o;
}
#endif

//

// returns at least req_div or -1 if we can't deliver it
static int find_divg(const div_range_t* r, unsigned req_div)
{
    if (r->mindiv >= req_div)
        return r->mindiv;
    if (r->maxdiv < req_div)
        return -1;

    unsigned k = (req_div - r->mindiv + r->step - 1) / r->step;
    unsigned div = r->mindiv + k * r->step;

    if (r->pinvlid) {
        unsigned j = 0;
        unsigned skp;
        do {
            skp = r->pinvlid[j++];
        } while (skp < div);

        for (; (div <= r->maxdiv) && (skp == div); div += r->step) {
            skp = r->pinvlid[j++];
        }
    }
    /*
    for (div = r->mindiv; div <= r->maxdiv; div += r->step) {
        if (div == skp) {
            skp = r->pinvlid[j++];
            continue;
        }
        if (div >= req_div)
            break;
    }*/

    return div;
}


// number of hertz we can tolerate
// out_freq_divs all divs from stage1 + ...
int find_best_vco(const vco_range_t* pvcos, unsigned vco_count,
                  const div_range_t* div, const unsigned div_cascade_count,
                  const uint64_t *freq_req, unsigned *out_freq_divs, const unsigned freq_count)
{
    enum {
        MAX_COUNT = 16,
    };

    uint64_t tolerance = 2500;
    uint64_t tolerance_d = 3;
    unsigned i, j;
    unsigned req_divs[freq_count]; //required div, may not be resolved
    unsigned *p_out_div[div_cascade_count];

    static const struct div_range nodiv = {
        .mindiv = 1,
        .maxdiv = 1,
        .step = 1,
        .count = 1,
        .pinvlid = NULL,
    };

    // find least common multiplier, but for now we expect the one in the group
    uint64_t lcm_freq = 0;

    for (i = 0, j = 0; i < div_cascade_count; i++) {
        p_out_div[i] = &out_freq_divs[j];
        j += div[i].count;
    }


    for (i = 0; i < freq_count; i++) {
        if (lcm_freq < freq_req[i])
            lcm_freq = freq_req[i];
    }
    for (i = 0; i < freq_count; i++) {
        if (lcm_freq % (freq_req[i] - tolerance_d) > 2 * tolerance) {
            return -2;
        }

        req_divs[i] = lcm_freq / (freq_req[i] - tolerance);
    }

    // find which VCO fits the capable dividers
    // we need to be first deviders to be max (prescalers)
    for (i = 0; i < vco_count; i++) {
        const struct div_range* div_next = (div_cascade_count > 1) ? &div[1] : &nodiv;
        unsigned nxt_cmb_div = pvcos[i].vcomin / lcm_freq / div[0].maxdiv;
        if (nxt_cmb_div > 1)
            nxt_cmb_div--;

        int fr;
        for (;;) {
            fr = find_divg(div_next, nxt_cmb_div + 1);
            if (fr <= 0)
                break;
            nxt_cmb_div = fr;
            uint64_t rfreq = lcm_freq * nxt_cmb_div;

            unsigned rdiv = pvcos[i].vcomax / rfreq;
            int sdiv = find_divg(&div[0], rdiv);
            if (sdiv < 0)
                continue;

            uint64_t vcofreq = rfreq * sdiv;
            if (pvcos[i].vcomax < vcofreq)
                continue;
            if (pvcos[i].vcomin > vcofreq)
                continue;

            // Found a good combination
            // [0]: sdiv
            // [1]: fr
            *p_out_div[0] = sdiv;
            for (j = 0; j < freq_count; j++) {
                p_out_div[1][j] = fr * req_divs[j];
            }

            // TODO: solution scoring
            return i;
        }
    }

    // Freqency can't be delivered
    return -1;
}

// Lanes per converter device (L)
// Samples per converter per frame (S)
// Octets per frame per lane (F)
// F=M*N'*S/(8*L)
// Frames per multiframe (K)


// K * F

uint64_t calc_serder_clock(uint64_t samplerte, unsigned f)
{
    return samplerte * f * 10;
}

uint32_t calc_lmfc_clock(uint64_t samplerte, unsigned k)
{
    return samplerte / k;
}
