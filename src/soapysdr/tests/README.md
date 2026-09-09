# SoapySDR USDR tests

This directory contains SoapySDR tests for the `usdr` module. The Python and C
smoke tests are hardware-in-the-loop tests: they exercise the installed Soapy
API against a real board and print a readable capability report. The
`test_rx_packet_buffer` target is a local unit test for packet buffering logic
and does not require hardware.

## Quick control-plane smoke test

```sh
python3 src/soapysdr/tests/soapy_usdr_hil.py --device "driver=usdr"
```

The script enumerates and opens the device, detects available RX/TX software
and hardware channels, checks common Get/List functions, validates ranges, and
round-trips safe control values for sample rate, frequency, bandwidth, and
gain where supported.

## RX streaming smoke test

```sh
python3 src/soapysdr/tests/soapy_usdr_hil.py \
    --device "driver=usdr" \
    --rx-stream \
    --rx-samples 4096 \
    --rx-reads 4
```

The stream test needs Python `numpy`, because SoapySDR Python bindings expect
array-like sample buffers for `readStream()`. When RX streaming is enabled, the
test also reads several sizes different from `bufferLength` to exercise the
packet buffering path and timestamp continuity.

By default, timestamp gaps are not filled and appear as timestamp jumps. Use
`--rx-gap-fill zero` to request zero-filled gaps:

```sh
python3 src/soapysdr/tests/soapy_usdr_hil.py \
    --device "driver=usdr" \
    --rx-stream \
    --rx-gap-fill zero
```

## TX streaming smoke test

TX streaming is opt-in because it transmits samples:

```sh
python3 src/soapysdr/tests/soapy_usdr_hil.py \
    --device "driver=usdr" \
    --tx-stream
```

The TX smoke test writes more samples than the stream MTU in one `writeStream()`
call, so it exercises the Soapy-side chunking path.

## C API smoke test

When `ENABLE_TESTS` is enabled, CMake builds a small C API smoke-test binary
from `test_usdr_soapy.c`:

```sh
cmake -S src -B build -DENABLE_TESTS=ON
cmake --build build --target test_usdr_soapy
build/soapysdr/tests/test_usdr_soapy -Q
```

Use `-Q` for query/control-plane checks only. Omit it to include RX streaming:

```sh
build/soapysdr/tests/test_usdr_soapy -c 2 -i 4096 -n 4
```

The C RX stream smoke test also performs variable-size `readStream()` calls
over the configured hardware packet size. Add `-Z` to enable zero-filled RX
timestamp gaps, or `-T` to include the TX `writeStream()` chunking smoke test.

## Packet buffer unit test

```sh
cmake -S src -B build -DENABLE_TESTS=ON
cmake --build build --target test_rx_packet_buffer
build/soapysdr/tests/test_rx_packet_buffer
```

This test validates variable RX read sizes, timestamp-gap no-fill mode, and
zero-fill mode including very large virtual gaps.

## CTest integration

Hardware tests are opt-in so CI without an SDR device remains green:

```sh
cmake -S src -B build -DENABLE_TESTS=ON -DENABLE_SOAPY_HIL_TESTS=ON
cmake --build build
ctest --test-dir build -R soapy_usdr_hil --output-on-failure
```

The packet-buffer unit test can be run without hardware:

```sh
ctest --test-dir build -R rx_packet_buffer --output-on-failure
```
