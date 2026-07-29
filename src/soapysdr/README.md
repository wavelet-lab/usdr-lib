# SoapySDR USDR support

This directory contains the `usdr` SoapySDR module and hardware-in-the-loop
tests. The tests are intentionally Python scripts, not unit tests: the goal is
to exercise the installed Soapy API against a real board and print a readable
capability report.

## Quick control-plane smoke test

```sh
python3 src/soapysdr/tests/soapy_usdr_hil.py --device "driver=usdr"
```

The script enumerates/open the device, detects available RX/TX software and
hardware channels, checks common Get/List functions, validates ranges, and
round-trips safe control values for sample rate, frequency, bandwidth, and
gain where supported.

## Device discovery

Use the `driver=usdr` filter when listing USDR devices. Without the driver key,
SoapySDR asks every installed module to enumerate, and unrelated modules such
as audio devices may appear in the output.

```sh
SoapySDRUtil --find="driver=usdr"
SoapySDRUtil --find="driver=usdr,bus=usb@5/1/10"
SoapySDRUtil --find="driver=usdr,bus=pci,device=usdr0"
```

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

TX streaming is opt-in because it transmits samples:

```sh
python3 src/soapysdr/tests/soapy_usdr_hil.py \
    --device "driver=usdr" \
    --tx-stream
```

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
over the configured hardware packet size. Add `-T` to include the TX
`writeStream()` chunking smoke test.

## CTest integration

Hardware tests are opt-in so CI without an SDR device remains green:

```sh
cmake -S src -B build -DENABLE_TESTS=ON -DENABLE_SOAPY_HIL_TESTS=ON
cmake --build build
ctest --test-dir build -R soapy_usdr_hil --output-on-failure
```
