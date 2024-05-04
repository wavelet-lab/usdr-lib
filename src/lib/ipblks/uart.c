// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include "uart.h"

// Register description


// [31]      - TX fifo empty
// [20:16]   - used elements in TX fifo

// [15]      - RX fifo empty
// [14:10]   - used elements in RX fifo

// [9]       - RX line state 1
// [8]       - RX line state 0

// [7:0]     - fifo data




int uart_core_init(lldev_t lldev, subdev_t subdev, unsigned base, uart_core_t* puac)
{
    puac->lldev = lldev;
    puac->subdev = subdev;
    puac->base = base;
    return 0;
}

int uart_core_rx_get(uart_core_t* puac, unsigned max_buf, char* data)
{
    int res;
    uint32_t reg;
    unsigned pos;

    for (pos = 0; pos < max_buf; pos++) {
        res = lowlevel_reg_rd32(puac->lldev, puac->subdev, puac->base, &reg);
        if (res)
            return res;

        if (reg & (1 << 15))
            break;

        data[pos] = reg;
    }

    return pos;
}


int uart_core_rx_collect(uart_core_t* puac, unsigned max_buf, char* data, unsigned timeout_ms)
{
    char *b = data;
    unsigned p = 0, sz = max_buf - 1;

    for(unsigned i = 0; i < timeout_ms / 10; i++) {
        int z = uart_core_rx_get(puac, sz - p, b + p);
        if (z < 0)
            return z;

        p += z;
        if (p == sz)
            break;

        usleep(10000);
    }

    b[p] = 0;
    for (unsigned h = 0; h < p; h++) {
        if (b[h] == '\n')
            continue;

        if (b[h] < 32)
            b[h] = 32;
    }

    return p;
}
