# SoapySDR USDR support

This directory contains the `usdr` SoapySDR module.

## Device discovery

Use the `driver=usdr` filter when listing USDR devices. Without the driver key,
SoapySDR asks every installed module to enumerate, and unrelated modules such
as audio devices may appear in the output.

USB devices can be selected by their topology path, for example
`bus=usb@3/1/6`: `3` is the USB bus, `1` is the downstream port, and `6` is the
device address. This is useful when several USDR boards are connected to stable
USB ports. It is not a serial number and may change if the board is moved to
another port or hub, and the device/address part may also change after
unplugging and plugging the board back into the same port.

```sh
SoapySDRUtil --find="driver=usdr"
SoapySDRUtil --find="driver=usdr,bus=usb@3/1/6"
SoapySDRUtil --find="driver=usdr,bus=pci,device=usdr0"
```

## Soapy Parameters

Device arguments are passed when enumerating or opening the device, for example
`SoapySDRUtil --find="driver=usdr,bus=usb@3/1/6"` or
`SoapySDR.Device({"driver": "usdr", "bus": "usb@3/1/6"})`.

| Argument | Scope | Description |
| --- | --- | --- |
| `driver=usdr` | discovery/open | Selects the USDR Soapy module. Use this to avoid unrelated devices from other Soapy modules. |
| `bus=<path>` | discovery/open | Selects a USB or PCI bus path, for example `usb@3/1/6` or `pci`. |
| `device=<name>` | discovery/open | Selects a lower-level device name, for example `usdr0` with `bus=pci`. |
| `dev=<kwargs>` | open | Packed lower-level device string. Explicit `bus`, `device`, `fe`, `extclk`, and `extref` arguments override values from `dev`. |
| `fe=<name>` | discovery/open | Selects a frontend when supported by the lower level. |
| `extclk=<value>` | discovery/open | Passes external clock selection to the lower level. |
| `extref=<value>` | discovery/open | Passes external reference selection to the lower level. |
| `rxGapFill=none` | open | Default RX timestamp gap fill mode for streams created by this device. |
| `rxGapFill=zero` | open | Default RX timestamp gap fill mode that fills timestamp gaps with zero samples. |

Stream arguments are passed to `setupStream()`.

| Argument | Direction | Default | Description |
| --- | --- | --- | --- |
| `bufferLength=<samples>` | RX/TX | automatic | Hardware packet size over the link. RX values must be either `0`/automatic or in the supported range checked by the driver. |
| `linkFormat=CS16` | RX/TX | `CS16` | Complex int16 link format. TX currently supports `CS16` only. |
| `linkFormat=CS12` | RX | `CS16` | Complex int12 link format for RX when the user stream format is `CF32`. |
| `floatScale=1.0` | RX/TX | `1.0` | Stream float scaling. Values other than `1.0` are currently rejected. |
| `rxGapFill=none` | RX | device default | Keeps only real samples and exposes packet loss as a timestamp jump. |
| `rxGapFill=zero` | RX | device default | Keeps only real samples internally, but fills timestamp gaps with zero samples when data is returned from `readStream()`. |

Advanced and debug open arguments are intended for development and diagnostics:

| Argument | Description |
| --- | --- |
| `loglevel=<n>` | Overrides USDR log level. The `SOAPY_USDR_LOGLEVEL` environment variable can also set the default on Linux. |
| `calls=1` | Enables verbose Soapy call logging. |
| `desired_rx_pkt=<samples>` | Overrides the default RX packet size used when `bufferLength` is not specified. |
| `rx12bit=1` | Forces RX wire format to 12-bit mode. |
| `rxdump=<file>` | Dumps received samples to a file for debugging. |
| `txcorr=<value>` | Applies the existing TX correction/debug path. |
| `refclk=<value>` | Recognized for reference clock selection; currently logs the request. |
| `rx_bw=<hz>` / `tx_bw=<hz>` | Applies a bandwidth value at open time. Prefer standard `setBandwidth()` for normal applications. |

`SOAPY_USDR_ARGS` may be used to override open arguments from the environment
with a packed Soapy kwargs string.

## Testing

See [tests/README.md](tests/README.md) for Python and C hardware-in-the-loop
smoke tests, stream buffer checks, and CTest integration.
