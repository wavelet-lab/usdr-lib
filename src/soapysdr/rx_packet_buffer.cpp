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
                               GapFill gap_fill,
                               RecvFunction recv_function)
    : _stream(stream)
    , _recv(recv_function ? recv_function : &usdr_dms_recv)
    , _channels(channels)
    , _samples_per_packet(samples_per_packet)
    , _bytes_per_packet(bytes_per_packet)
    , _bytes_per_sample(bytes_per_packet / samples_per_packet)
    , _gap_fill(gap_fill)
    , _capacity(0)
    , _read_pos(0)
    , _write_pos(0)
    , _available(0)
    , _next_output_time(0)
    , _time_valid(false)
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

size_t RxPacketBuffer::elemsPerBytes(size_t bytes) const
{
    return bytes / _bytes_per_sample;
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

    if (!_time_valid) {
        _next_output_time = nfo.fsymtime;
        _time_valid = true;
    }

    if (!_segments.empty()) {
        const Segment &last = _segments.back();
        const dm_time_t expected_time = last.start_time + (dm_time_t)last.samples;
        if (expected_time == nfo.fsymtime) {
            _segments.back().samples += packet_samples;
        } else if (_gap_fill == GAP_FILL_ZERO && nfo.fsymtime > expected_time) {
            _segments.push_back({nfo.fsymtime, packet_samples});
        } else {
            dropBufferedData();
            _next_output_time = nfo.fsymtime;
            _time_valid = true;
            _segments.push_back({nfo.fsymtime, packet_samples});
        }
    } else if (nfo.fsymtime < _next_output_time ||
               (_gap_fill == GAP_FILL_NONE && nfo.fsymtime != _next_output_time)) {
        dropBufferedData();
        _next_output_time = nfo.fsymtime;
        _time_valid = true;
        _segments.push_back({nfo.fsymtime, packet_samples});
    } else {
        _segments.push_back({nfo.fsymtime, packet_samples});
    }

    ensureCapacity(_available + packet_bytes);
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

void RxPacketBuffer::readRealBytes(void * const *buffs, size_t dst_offset_bytes, size_t bytes)
{
    for (unsigned ch = 0; ch < _channels; ch++) {
        unsigned char *dst = static_cast<unsigned char*>(buffs[ch]) + dst_offset_bytes;
        const size_t first = std::min(bytes, _capacity - _read_pos);
        std::memcpy(dst, _buffers[ch].data() + _read_pos, first);
        if (first < bytes) {
            std::memcpy(dst + first, _buffers[ch].data(), bytes - first);
        }
    }

    _read_pos = (_read_pos + bytes) % _capacity;
    _available -= bytes;
    size_t samples = elemsPerBytes(bytes);
    _next_output_time += (dm_time_t)samples;
    while (samples != 0 && !_segments.empty()) {
        Segment &segment = _segments.front();
        if (samples < segment.samples) {
            segment.start_time += (dm_time_t)samples;
            segment.samples -= samples;
            break;
        }
        samples -= segment.samples;
        _segments.pop_front();
    }
}

void RxPacketBuffer::writeZeros(void * const *buffs, size_t dst_offset_bytes, size_t bytes)
{
    for (unsigned ch = 0; ch < _channels; ch++) {
        unsigned char *dst = static_cast<unsigned char*>(buffs[ch]) + dst_offset_bytes;
        std::memset(dst, 0, bytes);
    }
    _next_output_time += (dm_time_t)elemsPerBytes(bytes);
}

size_t RxPacketBuffer::outputAvailableSamples() const
{
    if (!_time_valid || _segments.empty()) {
        return 0;
    }

    dm_time_t cursor = _next_output_time;
    size_t samples = 0;
    for (const Segment &segment: _segments) {
        if (segment.start_time > cursor) {
            if (_gap_fill != GAP_FILL_ZERO) {
                break;
            }
            samples += (size_t)(segment.start_time - cursor);
            cursor = segment.start_time;
        } else if (segment.start_time < cursor) {
            return 0;
        }

        samples += segment.samples;
        cursor += (dm_time_t)segment.samples;
    }
    return samples;
}

void RxPacketBuffer::dropBufferedData()
{
    _read_pos = 0;
    _write_pos = 0;
    _available = 0;
    _segments.clear();
}

int RxPacketBuffer::read(void * const *buffs,
                         size_t elems,
                         long timeout_us,
                         dm_time_t &sample_time,
                         usdr_dms_recv_nfo_t &nfo)
{
    const size_t requested_bytes = bytesPerElems(elems);
    ensureCapacity(requested_bytes + _bytes_per_packet);

    while (outputAvailableSamples() < elems) {
        const int res = _recv(_stream, _packet_ptrs.data(), timeout_us / 1000, &nfo);
        if (res != 0) {
            return res;
        }
        appendPacket(nfo);
    }

    sample_time = _next_output_time;
    size_t copied = 0;
    while (copied < requested_bytes) {
        if (_segments.empty()) {
            break;
        }

        const Segment &first = _segments.front();
        if (first.start_time > _next_output_time) {
            const size_t zero_samples = std::min((size_t)(first.start_time - _next_output_time),
                                                 elemsPerBytes(requested_bytes - copied));
            const size_t zero_bytes = bytesPerElems(zero_samples);
            writeZeros(buffs, copied, zero_bytes);
            copied += zero_bytes;
            continue;
        }

        const size_t real_bytes = std::min(bytesPerElems(first.samples), requested_bytes - copied);
        readRealBytes(buffs, copied, real_bytes);
        copied += real_bytes;
    }
    return 0;
}

void RxPacketBuffer::reset()
{
    dropBufferedData();
    _next_output_time = 0;
    _time_valid = false;
}

bool RxPacketBuffer::empty() const
{
    return _segments.empty() && _available == 0;
}
