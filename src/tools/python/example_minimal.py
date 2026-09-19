#!/usr/bin/env python3
"""Minimal usage example for high-level UsdrDevice/UsdrStream API."""

import argparse
import numpy as np

from usdr_bindings import USDR_DMS_START, USDR_DMS_STOP, UsdrDevice

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="USDR minimal RX/TX example")
    parser.add_argument("--device", default="", help="USDR device string")
    parser.add_argument("--loglevel", type=int, default=None, help="Optional libusdr log level")
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    with UsdrDevice(args.device, loglevel=args.loglevel) as dev:
        dev.set_samplerate(5_000_000)
        dev.configure_rf(
            rx_freq=900_000_000,
            tx_freq=920_000_000,
            rx_bandwidth=1_000_000,
            tx_bandwidth=1_000_000,
            rx_gain_lna=15,
            rx_gain_vga=15,
            rx_gain_pga=15,
            tx_gain=0,
            rx_path="rx_auto",
            tx_path="tx_auto",
        )

        with dev.create_stream("/ll/srx/0", "cf32", [0], 8*2048) as rx_stream, dev.create_stream("/ll/stx/0", "ci16", [0], 2048) as tx_stream:
            info = rx_stream.info()
            print(f"RX stream: channels={info.channels} pktsyms={info.pktsyms} pktbytes={info.pktbszie}")

            rx_stream.sync("any", tx_stream)
            rx_stream.op(USDR_DMS_START)
            tx_stream.op(USDR_DMS_START)

            rx_arrays, rx_nfo = rx_stream.recv(timeout_ms=1000, with_info=True)
            print(f"RX dtype={rx_arrays[0].dtype} shape={rx_arrays[0].shape}")
            print(f"Received symbols={rx_nfo.totsyms} lost={rx_nfo.totlost} hw_time={rx_nfo.fsymtime}")

            tx_data = np.zeros((info.pktsyms, 2), dtype=np.int16)
            tx_stream.send([tx_data], timestamp=0, timeout_ms=1000)

            rx_stream.op(USDR_DMS_STOP)
            tx_stream.op(USDR_DMS_STOP)


if __name__ == "__main__":
    main()
