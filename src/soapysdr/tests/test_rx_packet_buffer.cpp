// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "../rx_packet_buffer.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

struct FakeStream
{
    unsigned channels = 2;
    unsigned samples_per_packet = 4;
    dm_time_t next_time = 100;
    unsigned recv_count = 0;
    bool jump_after_first_packet = false;
};

static int fake_recv(pusdr_dms_t stream, void **buffs, unsigned /*timeout_ms*/, usdr_dms_recv_nfo_t *nfo)
{
    FakeStream *fake = reinterpret_cast<FakeStream*>(stream);

    for (unsigned ch = 0; ch < fake->channels; ch++) {
        int16_t *out = static_cast<int16_t*>(buffs[ch]);
        for (unsigned i = 0; i < fake->samples_per_packet; i++) {
            out[i] = (int16_t)(ch * 1000 + fake->recv_count * fake->samples_per_packet + i);
        }
    }

    nfo->fsymtime = fake->next_time;
    nfo->totsyms = fake->samples_per_packet;
    nfo->totlost = 0;
    nfo->max_parts = 0;
    nfo->extra = 0;

    fake->recv_count++;
    if (fake->jump_after_first_packet && fake->recv_count == 1) {
        fake->next_time = 999;
    } else {
        fake->next_time += fake->samples_per_packet;
    }

    return 0;
}

static void assert_samples(const std::vector<int16_t> &samples, int first_value)
{
    for (size_t i = 0; i < samples.size(); i++) {
        assert(samples[i] == first_value + (int)i);
    }
}

static void test_variable_read_sizes()
{
    FakeStream fake;
    RxPacketBuffer buffer(reinterpret_cast<pusdr_dms_t>(&fake),
                          fake.channels,
                          fake.samples_per_packet,
                          fake.samples_per_packet * sizeof(int16_t),
                          fake_recv);

    usdr_dms_recv_nfo_t nfo = {};
    dm_time_t sample_time = 0;

    std::vector<int16_t> ch0(5);
    std::vector<int16_t> ch1(5);
    void *buffs[] = { ch0.data(), ch1.data() };

    int res = buffer.read(buffs, 3, 100000, sample_time, nfo);
    assert(res == 0);
    assert(sample_time == 100);
    assert(fake.recv_count == 1);
    assert_samples(std::vector<int16_t>(ch0.begin(), ch0.begin() + 3), 0);
    assert_samples(std::vector<int16_t>(ch1.begin(), ch1.begin() + 3), 1000);

    std::memset(ch0.data(), 0, ch0.size() * sizeof(ch0[0]));
    std::memset(ch1.data(), 0, ch1.size() * sizeof(ch1[0]));
    res = buffer.read(buffs, 5, 100000, sample_time, nfo);
    assert(res == 0);
    assert(sample_time == 103);
    assert(fake.recv_count == 2);
    assert_samples(ch0, 3);
    assert_samples(ch1, 1003);

    std::memset(ch0.data(), 0, ch0.size() * sizeof(ch0[0]));
    std::memset(ch1.data(), 0, ch1.size() * sizeof(ch1[0]));
    res = buffer.read(buffs, 2, 100000, sample_time, nfo);
    assert(res == 0);
    assert(sample_time == 108);
    assert(fake.recv_count == 3);
    assert_samples(std::vector<int16_t>(ch0.begin(), ch0.begin() + 2), 8);
    assert_samples(std::vector<int16_t>(ch1.begin(), ch1.begin() + 2), 1008);
}

static void test_timestamp_gap_drops_buffered_tail()
{
    FakeStream fake;
    fake.jump_after_first_packet = true;
    RxPacketBuffer buffer(reinterpret_cast<pusdr_dms_t>(&fake),
                          fake.channels,
                          fake.samples_per_packet,
                          fake.samples_per_packet * sizeof(int16_t),
                          fake_recv);

    usdr_dms_recv_nfo_t nfo = {};
    dm_time_t sample_time = 0;

    std::vector<int16_t> ch0(4);
    std::vector<int16_t> ch1(4);
    void *buffs[] = { ch0.data(), ch1.data() };

    int res = buffer.read(buffs, 2, 100000, sample_time, nfo);
    assert(res == 0);
    assert(sample_time == 100);
    assert(fake.recv_count == 1);
    assert_samples(std::vector<int16_t>(ch0.begin(), ch0.begin() + 2), 0);

    res = buffer.read(buffs, 4, 100000, sample_time, nfo);
    assert(res == 0);
    assert(sample_time == 999);
    assert(fake.recv_count == 2);
    assert_samples(ch0, 4);
    assert_samples(ch1, 1004);
}

int main()
{
    test_variable_read_sizes();
    test_timestamp_gap_drops_buffered_tail();

    std::cout << "RxPacketBuffer tests passed" << std::endl;
    return 0;
}
