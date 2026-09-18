#!/usr/bin/env python3
"""Receive IQ samples from USDR and plot accumulated FFT magnitude (using SoapySDR).

Example:
    python3 example_soapy_rx.py \
        --device "driver=usdr" \
        --rx-frequency 900e6 \
        --rx-bandwidth 1e6 \
        --rx-gain 80 \
        --samplerate 50e6 \
        --accumulation 16 \
        --fft-size 4096 \
        --window hann \
        --loglevel 3
        
    Parameter `device` can be any SoapySDR device string, e.g. "driver=usdr" or "driver=usdr,bus=<device_bus>"
    device_bus can be:
        - bus=pci,device=<device_path>, where <device_path> is the PCI device Path (e.g. /dev/usdr0)
        - bus=usb@<usb_address>, where <usb_address> is the USB address (e.g. 3/1/6 for bus 3, port 1, device 6)
"""

from __future__ import annotations

import argparse
import numpy as np
from scipy import signal
import matplotlib.pyplot as plt

import SoapySDR
from SoapySDR import SOAPY_SDR_RX


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
    parser.add_argument("--calibrate", action="store_true", help="Enable LO & IQ Imbalance calibration")

    return parser.parse_args()

# Allocate a 64-byte-aligned buffer to avoid AVX512 alignment issues
def aligned_empty(shape, dtype, alignment=64):
    dtype = np.dtype(dtype)
    n_elems = int(np.prod(shape))
    nbytes = n_elems * dtype.itemsize
    raw = np.empty(nbytes + alignment, dtype=np.uint8)
    start = raw.ctypes.data
    offset = (-start) % alignment
    buf = raw[offset:offset + nbytes].view(dtype)
    return buf.reshape(shape)


def main() -> None:
    args = parse_args()

    if args.fft_size <= 0:
        raise ValueError("--fft-size must be > 0")
    if args.accumulation <= 0:
        raise ValueError("--accumulation must be > 0")

    fft_size = int(args.fft_size)
    window = signal.get_window(args.window, fft_size, fftbins=True).astype(np.float32)

    # Open SoapySDR device
    if args.device:
        dev_kwargs = SoapySDR.KwargsFromString(args.device)
    else:
        dev_kwargs = SoapySDR.SoapySDRKwargs()
    if args.loglevel is not None:
        dev_kwargs["loglevel"] = str(int(args.loglevel))
    try:
        dev = SoapySDR.Device(dev_kwargs)
    except Exception as e:
        print(f"Failed to open SoapySDR device: {e}")
        exit(1)

    chan = int(args.channel)

    # Configure device
    dev.setSampleRate(SOAPY_SDR_RX, chan, float(args.samplerate))
    dev.setFrequency(SOAPY_SDR_RX, chan, float(args.rx_frequency))
    dev.setBandwidth(SOAPY_SDR_RX, chan, float(args.rx_bandwidth))
    dev.setGain(SOAPY_SDR_RX, chan, float(args.rx_gain))

    # Setup stream for complex float32 (direction, format, channels, args)
    args_kw = SoapySDR.SoapySDRKwargs()
    args_kw["linkFormat"] = "CS16"  # Request int16 samples from driver also can be "CS12"
    args_kw["bufferLength"] = str(int(fft_size))
    rx_stream = dev.setupStream(SOAPY_SDR_RX, "CF32", [chan], args_kw)
    dev.activateStream(rx_stream)

    psd_acc = np.zeros(fft_size, dtype=np.float64)

    timeout_us = int(args.timeout_ms * 1000)

    if args.calibrate:
        dev.writeSetting("calibrate", "rx")

    # Receive and accumulate FFT frames
    for _ in range(int(args.accumulation)):
        buf = aligned_empty((fft_size,), np.complex64, alignment=64)

        res = dev.readStream(rx_stream, [buf], int(fft_size), timeoutUs=timeout_us)

        n = res.ret
        if n < fft_size:
            raise RuntimeError(f"Received {n} samples, expected at least {fft_size}")

        x = buf[:fft_size] * window
        spec = np.fft.fftshift(np.fft.fft(x, n=fft_size))
        psd_acc += np.abs(spec) ** 2

    # Close stream
    dev.deactivateStream(rx_stream)
    dev.closeStream(rx_stream)

    # Close device
    if hasattr(dev, 'close'):
        dev.close()  # Not strictly necessary, but good practice
    dev = None

    psd = psd_acc / args.accumulation
    psd_db = 10.0 * np.log10(psd + 1e-20)

    fs = float(args.samplerate)
    f_axis = np.fft.fftshift(np.fft.fftfreq(fft_size, d=1.0 / fs))

    plt.figure(figsize=(10, 5))
    plt.plot(f_axis / 1e6, psd_db)
    plt.title("USDR RX FFT (SoapySDR)")
    plt.xlabel("Frequency offset (MHz)")
    plt.ylabel("Power (dB, arbitrary)")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
