// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "rx_packet_buffer.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

RxPacketBuffer::RxPacketBuffer(pusdr_dms_t stream,
                               unsigned channels,
                               size_t samples_per_packet,
                               size_t bytes_per_packet,
                               RecvFunction recv_function)
    : _stream(stream)
    , _recv(recv_function ? recv_function : &usdr_dms_recv)
    , _channels(channels)
    , _samples_per_packet(samples_per_packet)
    , _bytes_per_packet(bytes_per_packet)
    , _bytes_per_sample(bytes_per_packet / samples_per_packet)
    , _capacity(0)
    , _read_pos(0)
    , _write_pos(0)
    , _available(0)
    , _first_sample_time(0)
{
    if (_stream == nullptr || _channels == 0 || _samples_per_packet == 0 ||
        _bytes_per_packet == 0 || _bytes_per_sample == 0 ||
        (_bytes_per_packet % _samples_per_packet) != 0) {
        throw std::invalid_argument("RxPacketBuffer: invalid stream packet configuration");
    }

    _packet_buffers.resize(_channels);
    _packet_ptrs.resize(_channels);
    for (unsigned i = 0; i < _channels; i++) {
        _packet_buffers[i].resize(_bytes_per_packet);
        _packet_ptrs[i] = _packet_buffers[i].data();
    }

    ensureCapacity(_bytes_per_packet * 16);
}

size_t RxPacketBuffer::bytesPerElems(size_t elems) const
{
    return elems * _bytes_per_sample;
}

void RxPacketBuffer::ensureCapacity(size_t requested_bytes)
{
    if (requested_bytes <= _capacity) {
        return;
    }

    size_t new_capacity = std::max(_bytes_per_packet * 16, _capacity);
    if (new_capacity == 0) {
        new_capacity = _bytes_per_packet * 16;
    }
    while (new_capacity < requested_bytes) {
        new_capacity *= 2;
    }

    std::vector<std::vector<unsigned char>> new_buffers(_channels);
    for (unsigned ch = 0; ch < _channels; ch++) {
        new_buffers[ch].resize(new_capacity);
        if (_available == 0) {
            continue;
        }

        const size_t first = std::min(_available, _capacity - _read_pos);
        std::memcpy(new_buffers[ch].data(), _buffers[ch].data() + _read_pos, first);
        if (first < _available) {
            std::memcpy(new_buffers[ch].data() + first, _buffers[ch].data(), _available - first);
        }
    }

    _buffers.swap(new_buffers);
    _capacity = new_capacity;
    _read_pos = 0;
    _write_pos = _available;
}

void RxPacketBuffer::appendPacket(const usdr_dms_recv_nfo_t &nfo)
{
    const size_t reported_samples = (nfo.totsyms != 0) ? nfo.totsyms : _samples_per_packet;
    const size_t packet_samples = std::min(reported_samples, _samples_per_packet);
    const size_t packet_bytes = bytesPerElems(packet_samples);
    ensureCapacity(_available + packet_bytes);

    if (_available == 0) {
        _first_sample_time = nfo.fsymtime;
    } else {
        const dm_time_t expected_time = _first_sample_time + (dm_time_t)(_available / _bytes_per_sample);
        if (expected_time != nfo.fsymtime) {
            reset();
            _first_sample_time = nfo.fsymtime;
        }
    }

    for (unsigned ch = 0; ch < _channels; ch++) {
        const size_t first = std::min(packet_bytes, _capacity - _write_pos);
        std::memcpy(_buffers[ch].data() + _write_pos, _packet_buffers[ch].data(), first);
        if (first < packet_bytes) {
            std::memcpy(_buffers[ch].data(), _packet_buffers[ch].data() + first, packet_bytes - first);
        }
    }

    _write_pos = (_write_pos + packet_bytes) % _capacity;
    _available += packet_bytes;
}

void RxPacketBuffer::readBytes(void * const *buffs, size_t bytes)
{
    for (unsigned ch = 0; ch < _channels; ch++) {
        unsigned char *dst = static_cast<unsigned char*>(buffs[ch]);
        const size_t first = std::min(bytes, _capacity - _read_pos);
        std::memcpy(dst, _buffers[ch].data() + _read_pos, first);
        if (first < bytes) {
            std::memcpy(dst + first, _buffers[ch].data(), bytes - first);
        }
    }

    _read_pos = (_read_pos + bytes) % _capacity;
    _available -= bytes;
    _first_sample_time += bytes / _bytes_per_sample;
}

int RxPacketBuffer::read(void * const *buffs,
                         size_t elems,
                         long timeout_us,
                         dm_time_t &sample_time,
                         usdr_dms_recv_nfo_t &nfo)
{
    const size_t requested_bytes = bytesPerElems(elems);
    ensureCapacity(requested_bytes + _bytes_per_packet);

    while (_available < requested_bytes) {
        const int res = _recv(_stream, _packet_ptrs.data(), timeout_us / 1000, &nfo);
        if (res != 0) {
            return res;
        }
        appendPacket(nfo);
    }

    sample_time = _first_sample_time;
    readBytes(buffs, requested_bytes);
    return 0;
}

void RxPacketBuffer::reset()
{
    _read_pos = 0;
    _write_pos = 0;
    _available = 0;
    _first_sample_time = 0;
}
