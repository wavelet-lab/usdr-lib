// Copyright (c) 2023-2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef USDR_SOAPY_DECIM_H
#define USDR_SOAPY_DECIM_H

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

// Host-side decimation of one channel of complex float samples by a power of two,
// as a cascade of identical half-band FIR stages that each halve the rate.
//
// Each stage is a 63-tap half-band filter with a Kaiser window (beta 9.4). Up to 0.4 of
// the output rate (80% of the output Nyquist band) the passband is flat within 0.001 dB,
// and everything that aliases into it is attenuated by more than 90 dB.
class UsdrHalfbandDecimator
{
public:
    enum { TAPS = 63, CENTER = (TAPS - 1) / 2 };

    explicit UsdrHalfbandDecimator(unsigned factor = 1)
        : _factor(factor)
    {
        const double pi = 3.14159265358979323846;
        const double beta = 9.4;

        // Odd taps around the center are the only non-zero ones besides the center
        double sum = 0.5;
        for (unsigned j = 0; j < CENTER; j += 2) {
            const int n = static_cast<int>(j) - static_cast<int>(CENTER);
            const double r = static_cast<double>(n) / CENTER;
            const double window = bessel_i0(beta * std::sqrt(1.0 - r * r)) / bessel_i0(beta);
            const double tap = std::sin(pi * n / 2.0) / (pi * n) * window;
            _side.push_back(tap);
            sum += 2.0 * tap;
        }
        for (double &tap : _side) {
            tap /= sum;
        }
        _center = 0.5 / sum;

        for (unsigned f = factor; f > 1; f /= 2) {
            _stages.emplace_back();
        }
    }

    unsigned factor() const { return _factor; }

    // Decimates `count` input samples, a multiple of factor(), into count / factor() outputs.
    // `out` may not alias `in`.
    void process(const std::complex<float> *in, size_t count, std::complex<float> *out)
    {
        if (_stages.empty()) {
            std::copy(in, in + count, out);
            return;
        }

        const std::complex<float> *src = in;
        size_t n = count;
        for (size_t s = 0; s < _stages.size(); s++) {
            std::complex<float> *dst = (s + 1 == _stages.size()) ? out : _stages[s].out(n / 2);
            _stages[s].run(src, n, dst, _side, _center);
            src = dst;
            n /= 2;
        }
    }

    void reset()
    {
        for (Stage &stage : _stages) {
            stage.history.assign(TAPS - 1, std::complex<float>());
        }
    }

private:
    // Modified Bessel function of the first kind, order 0, for the Kaiser window
    static double bessel_i0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (unsigned k = 1; k < 50; k++) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1e-17)
                break;
        }
        return sum;
    }

    struct Stage
    {
        std::vector<std::complex<float>> history = std::vector<std::complex<float>>(TAPS - 1);
        std::vector<std::complex<float>> work;
        std::vector<std::complex<float>> output;

        std::complex<float> *out(size_t count)
        {
            if (output.size() < count) {
                output.resize(count);
            }
            return output.data();
        }

        void run(const std::complex<float> *in, size_t count, std::complex<float> *dst,
                 const std::vector<double> &side, double center)
        {
            // work = history (TAPS - 1 samples) followed by this block's input
            work.resize(TAPS - 1 + count);
            std::copy(history.begin(), history.end(), work.begin());
            std::copy(in, in + count, work.begin() + (TAPS - 1));

            const float c = static_cast<float>(center);
            for (size_t k = 0; k < count / 2; k++) {
                const std::complex<float> *x = work.data() + 2 * k;
                std::complex<float> acc = c * x[CENTER];
                for (unsigned p = 0; p < side.size(); p++) {
                    const unsigned j = 2 * p;
                    acc += static_cast<float>(side[p]) * (x[j] + x[TAPS - 1 - j]);
                }
                dst[k] = acc;
            }

            std::copy(work.end() - (TAPS - 1), work.end(), history.begin());
        }
    };

    unsigned _factor;
    std::vector<double> _side;
    double _center = 0.5;
    std::vector<Stage> _stages;
};

#endif
