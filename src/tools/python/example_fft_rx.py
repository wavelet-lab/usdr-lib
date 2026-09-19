#!/usr/bin/env python3
"""Receive IQ samples from USDR and plot accumulated FFT magnitude.

Example:
    python3 example_fft_rx.py \
        --device "" \
        --rx-frequency 900e6 \
        --rx-bandwidth 1e6 \
        --rx-gain 15 \
        --samplerate 50e6 \
        --accumulation 16 \
        --fft-size 4096 \
        --window hann
"""

from __future__ import annotations

import argparse
import numpy as np
from scipy import signal
import matplotlib.pyplot as plt

from usdr_bindings import USDR_DMS_START, USDR_DMS_STOP, UsdrDevice


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="USDR RX FFT plotter")
    parser.add_argument("--device", default="", help="USDR device string")
    parser.add_argument("--samplerate", type=float, default=5e6, help="Sample rate in SPS")
    parser.add_argument("--loglevel", type=int, default=None, help="Optional libusdr log level")
    parser.add_argument("--rx-frequency", type=float, required=True, help="RX frequency in Hz")
    parser.add_argument("--rx-bandwidth", type=float, required=True, help="RX bandwidth in Hz")
    parser.add_argument("--rx-gain", type=float, default=15, help="RX gain value (mapped to LNA gain)")

    parser.add_argument("--fft-size", type=int, default=4096, help="FFT bin size")
    parser.add_argument("--accumulation", type=int, default=16, help="Number of FFT frames to accumulate")
    parser.add_argument("--window", default="hann", help="Scipy window name (e.g. hann, blackman, flattop)")
    parser.add_argument("--channel", type=int, default=0, help="RX channel index")
    parser.add_argument("--timeout-ms", type=int, default=1000, help="Receive timeout in milliseconds")

    return parser.parse_args()


def main() -> None:
    args = parse_args()

    if args.fft_size <= 0:
        raise ValueError("--fft-size must be > 0")
    if args.accumulation <= 0:
        raise ValueError("--accumulation must be > 0")

    fft_size = int(args.fft_size)
    window = signal.get_window(args.window, fft_size, fftbins=True).astype(np.float32)

    with UsdrDevice(args.device, loglevel=args.loglevel) as dev:
        dev.set_samplerate(int(args.samplerate))
        dev.set_rx_frequency(int(args.rx_frequency))
        dev.set_rx_bandwidth(int(args.rx_bandwidth))
        dev.set_rx_gain_lna(int(args.rx_gain))

        with dev.create_stream(
            sobj="/ll/srx/0",
            dformat="cf32",
            channels=[int(args.channel)],
            pktsyms=fft_size,
        ) as rx_stream:
            rx_stream.sync("any")
            rx_stream.op(USDR_DMS_START)

            psd_acc = np.zeros(fft_size, dtype=np.float64)

            for _ in range(args.accumulation):
                arrays, _ = rx_stream.recv(timeout_ms=args.timeout_ms, with_info=False)
                iq = arrays[0]

                if iq.shape[0] < fft_size:
                    raise RuntimeError(f"Received {iq.shape[0]} samples, expected at least {fft_size}")

                x = iq[:fft_size] * window
                spec = np.fft.fftshift(np.fft.fft(x, n=fft_size))
                psd_acc += np.abs(spec) ** 2

            rx_stream.op(USDR_DMS_STOP)

    psd = psd_acc / args.accumulation
    psd_db = 10.0 * np.log10(psd + 1e-20)

    fs = float(args.samplerate)
    f_axis = np.fft.fftshift(np.fft.fftfreq(fft_size, d=1.0 / fs))

    plt.figure(figsize=(10, 5))
    plt.plot(f_axis / 1e6, psd_db)
    plt.title("USDR RX FFT")
    plt.xlabel("Frequency offset (MHz)")
    plt.ylabel("Power (dB, arbitrary)")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
