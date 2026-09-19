# SoapySDR USDR hardware tests

This directory contains hardware-in-the-loop tests for the `usdr` SoapySDR
module. They are intentionally Python scripts, not unit tests: the goal is to
exercise the installed Soapy API against a real board and print a readable
capability report.

## Quick control-plane smoke test

```sh
python3 src/soapysdr/tests/soapy_usdr_hil.py --device "driver=usdr"
```

The script enumerates/open the device, detects available RX/TX software and
hardware channels, checks common Get/List functions, validates ranges, and
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
array-like sample buffers for `readStream()`.

## C API smoke test

When `ENABLE_TESTS` is enabled, CMake builds a small C API smoke-test binary
from `test_usdr_soapy.c`:

```sh
cmake -S src -B build -DENABLE_TESTS=ON
cmake --build build --target test_usdr_soapy
build/soapysdr/test_usdr_soapy -Q
```

Use `-Q` for query/control-plane checks only. Omit it to include RX streaming:

```sh
build/soapysdr/test_usdr_soapy -c 2 -i 4096 -n 4
```

## CTest integration

Hardware tests are opt-in so CI without an SDR device remains green:

```sh
cmake -S src -B build -DENABLE_TESTS=ON -DENABLE_SOAPY_HIL_TESTS=ON
cmake --build build
ctest --test-dir build -R soapy_usdr_hil --output-on-failure
```
