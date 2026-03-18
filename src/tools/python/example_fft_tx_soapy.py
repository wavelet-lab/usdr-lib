#!/usr/bin/env python3
"""Generate a sine wave and transmit IQ samples via USDR (using SoapySDR).

Example:
    python3 example_fft_tx_soapy.py \
        --device "driver=usdr" \
        --tx-frequency 900e6 \
        --tx-bandwidth 5e6 \
        --tx-gain 16 \
        --samplerate 5e6 \
        --tone-offset 1000e3 \
        --amplitude 0.9 \
        --burst-size 4096 \
        --num-bursts 8192 \
        --loglevel 3

    Parameter `device` can be any SoapySDR device string, e.g. "driver=usdr" or "driver=usdr,bus=<device_bus>"
    device_bus can be:
        - bus=pci,device=<device_path>, where <device_path> is the PCI device Path (e.g. /dev/usdr0)
        - bus=usb@<usb_address>, where <usb_address> is the USB address (e.g. 3/3/6 for bus 3, device 3, function 6)
"""

from __future__ import annotations

import argparse
import numpy as np

import SoapySDR
from SoapySDR import SOAPY_SDR_TX


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="USDR TX sine wave generator")
    parser.add_argument("--device", default="", help="USDR device string")
    parser.add_argument("--samplerate", type=float, default=5e6, help="Sample rate in SPS")
    parser.add_argument("--loglevel", type=int, default=None, help="Optional libusdr log level")
    parser.add_argument("--tx-frequency", type=float, required=True, help="TX center frequency in Hz")
    parser.add_argument("--tx-bandwidth", type=float, required=True, help="TX bandwidth in Hz")
    parser.add_argument("--tx-gain", type=float, default=0, help="TX gain value")

    parser.add_argument("--tone-offset", type=float, default=100e3, help="Tone frequency offset from center in Hz")
    parser.add_argument("--amplitude", type=float, default=0.9, help="Tone amplitude (0.0 .. 1.0)")
    parser.add_argument("--burst-size", type=int, default=4096, help="Number of samples per write burst")
    parser.add_argument("--num-bursts", type=int, default=128, help="Number of bursts to transmit (0 = infinite)")
    parser.add_argument("--channel", type=int, default=0, help="TX channel index")
    parser.add_argument("--timeout-ms", type=int, default=1000, help="Write timeout in milliseconds")

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


def generate_tone(n_samples: int, samplerate: float, tone_offset: float,
                  amplitude: float, phase_offset: float = 0.0) -> tuple[np.ndarray, float]:
    """Generate a complex sine tone and return (samples, next_phase_offset).

    Keeping track of *phase_offset* across successive calls ensures a continuous
    waveform without discontinuities at burst boundaries.
    """
    t = np.arange(n_samples, dtype=np.float64) / samplerate
    phase = 2.0 * np.pi * tone_offset * t + phase_offset
    iq = amplitude * np.exp(1j * phase)

    # Compute the phase right after the last sample so the next call can continue
    next_phase = phase[-1] + 2.0 * np.pi * tone_offset / samplerate
    next_phase = next_phase % (2.0 * np.pi)

    return iq.astype(np.complex64), next_phase


def main() -> None:
    args = parse_args()

    if args.burst_size <= 0:
        raise ValueError("--burst-size must be > 0")
    if args.amplitude < 0.0 or args.amplitude > 1.0:
        raise ValueError("--amplitude must be in [0.0, 1.0]")

    burst_size = int(args.burst_size)

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
    dev.setSampleRate(SOAPY_SDR_TX, chan, float(args.samplerate))
    dev.setFrequency(SOAPY_SDR_TX, chan, float(args.tx_frequency))
    dev.setBandwidth(SOAPY_SDR_TX, chan, float(args.tx_bandwidth))
    dev.setGain(SOAPY_SDR_TX, chan, float(args.tx_gain))

    # Setup stream for complex float32 (direction, format, channels, args)
    args_kw = SoapySDR.SoapySDRKwargs()
    args_kw["linkFormat"] = "CS16"  # Request int16 samples from driver, also can be "CS12"
    args_kw["bufferLength"] = str(int(burst_size))
    tx_stream = dev.setupStream(SOAPY_SDR_TX, "CF32", [chan], args_kw)
    dev.activateStream(tx_stream)

    timeout_us = int(args.timeout_ms * 1000)
    phase = 0.0
    num_bursts = int(args.num_bursts)
    infinite = num_bursts == 0

    print(f"Transmitting tone at {args.tx_frequency + args.tone_offset:.0f} Hz "
          f"(center {args.tx_frequency:.0f} Hz + offset {args.tone_offset:.0f} Hz)")
    print(f"  samplerate={args.samplerate:.0f}  amplitude={args.amplitude}  "
          f"burst_size={burst_size}  num_bursts={'inf' if infinite else num_bursts}")

    # Transmit sine-wave bursts
    burst_idx = 0
    try:
        while infinite or burst_idx < num_bursts:
            tone, phase = generate_tone(burst_size, float(args.samplerate),
                                        float(args.tone_offset), float(args.amplitude),
                                        phase)

            buf = aligned_empty((burst_size,), np.complex64, alignment=64)
            buf[:] = tone

            res = dev.writeStream(tx_stream, [buf], int(burst_size), timeoutUs=timeout_us)

            n = res.ret
            if n < 0:
                raise RuntimeError(f"writeStream returned error code {n}")
            if n < burst_size:
                print(f"Warning: wrote only {n}/{burst_size} samples")

            burst_idx += 1
    except KeyboardInterrupt:
        print(f"\nInterrupted after {burst_idx} bursts")

    # Close stream
    dev.deactivateStream(tx_stream)
    dev.closeStream(tx_stream)

    # Close device
    if dev.close is not None:
        dev.close()  # Not strictly necessary, but good practice
    dev = None

    print("Done.")


if __name__ == "__main__":
    main()
