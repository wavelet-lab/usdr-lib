// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef RX_PACKET_BUFFER_H
#define RX_PACKET_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../lib/models/dm_stream.h"

class RxPacketBuffer
{
public:
    typedef int (*RecvFunction)(pusdr_dms_t stream,
                                void **buffs,
                                unsigned timeout_ms,
                                usdr_dms_recv_nfo_t *nfo);

    RxPacketBuffer(pusdr_dms_t stream,
                   unsigned channels,
                   size_t samples_per_packet,
                   size_t bytes_per_packet,
                   RecvFunction recv_function = nullptr);

    int read(void * const *buffs,
             size_t elems,
             long timeout_us,
             dm_time_t &sample_time,
             usdr_dms_recv_nfo_t &nfo);

    void reset();

private:
    size_t bytesPerElems(size_t elems) const;
    void ensureCapacity(size_t requested_bytes);
    void appendPacket(const usdr_dms_recv_nfo_t &nfo);
    void readBytes(void * const *buffs, size_t bytes);

    pusdr_dms_t _stream;
    RecvFunction _recv;
    unsigned _channels;
    size_t _samples_per_packet;
    size_t _bytes_per_packet;
    size_t _bytes_per_sample;

    std::vector<std::vector<unsigned char>> _buffers;
    std::vector<std::vector<unsigned char>> _packet_buffers;
    std::vector<void*> _packet_ptrs;

    size_t _capacity;
    size_t _read_pos;
    size_t _write_pos;
    size_t _available;
    dm_time_t _first_sample_time;
};

#endif
