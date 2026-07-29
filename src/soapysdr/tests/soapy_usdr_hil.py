#!/usr/bin/env python3
"""Hardware-in-the-loop smoke tests for the USDR SoapySDR module."""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import SoapySDR
    from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_TX
except Exception as exc:  # pragma: no cover - depends on host install
    print(f"FAIL: unable to import SoapySDR Python bindings: {exc}", file=sys.stderr)
    sys.exit(2)


Direction = Tuple[int, str]
DIRECTIONS: Tuple[Direction, ...] = (
    (SOAPY_SDR_RX, "RX"),
    (SOAPY_SDR_TX, "TX"),
)


class SkipCheck(Exception):
    """Signal that a check cannot be performed in this host binding."""


def setting_from_string(value: str) -> Dict[str, str]:
    if not value:
        return {}
    if hasattr(SoapySDR, "KwargsFromString"):
        return dict(SoapySDR.KwargsFromString(value))
    out: Dict[str, str] = {}
    for part in value.split(","):
        key, _, val = part.partition("=")
        if key:
            out[key.strip()] = val.strip()
    return out


def range_min(rng: Any) -> float:
    val = getattr(rng, "minimum", None)
    if val is None:
        val = getattr(rng, "start", None)
    return float(val() if callable(val) else val)


def range_max(rng: Any) -> float:
    val = getattr(rng, "maximum", None)
    if val is None:
        val = getattr(rng, "stop", None)
    return float(val() if callable(val) else val)


def ranges_to_json(ranges: Sequence[Any]) -> List[Dict[str, float]]:
    return [{"minimum": range_min(rng), "maximum": range_max(rng)} for rng in ranges]


def in_ranges(value: float, ranges: Sequence[Any]) -> bool:
    return any(range_min(rng) <= value <= range_max(rng) for rng in ranges)


def pick_in_range(ranges: Sequence[Any], preferred: float) -> float:
    if not ranges:
        return preferred
    for rng in ranges:
        lo = range_min(rng)
        hi = range_max(rng)
        if lo <= preferred <= hi:
            return preferred
    lo = range_min(ranges[0])
    hi = range_max(ranges[0])
    if hi <= lo:
        return lo
    return lo + (hi - lo) * 0.25


class Runner:
    def __init__(self, verbose: bool = False) -> None:
        self.verbose = verbose
        self.failures: List[str] = []
        self.skips: List[str] = []
        self.report: Dict[str, Any] = {"checks": []}

    def check(self, name: str, fn: Callable[[], Any]) -> Any:
        try:
            result = fn()
            self.report["checks"].append({"name": name, "status": "PASS"})
            print(f"PASS {name}")
            if self.verbose:
                print(f"  {json.dumps(result, sort_keys=True)}")
            return result
        except SkipCheck as exc:
            self.skip(name, str(exc))
            return None
        except Exception as exc:
            self.failures.append(f"{name}: {exc}")
            self.report["checks"].append({"name": name, "status": "FAIL", "error": str(exc)})
            print(f"FAIL {name}: {exc}")
            return None

    def skip(self, name: str, reason: str) -> None:
        self.skips.append(f"{name}: {reason}")
        self.report["checks"].append({"name": name, "status": "SKIP", "reason": reason})
        print(f"SKIP {name}: {reason}")

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            raise AssertionError(message)


def enumerate_devices(device_args: Dict[str, str]) -> List[Dict[str, str]]:
    enum_args = dict(device_args)
    if "driver" not in enum_args:
        enum_args["driver"] = "usdr"
    return [dict(item) for item in SoapySDR.Device.enumerate(enum_args)]


def make_device(device_args: Dict[str, str]) -> Any:
    args = dict(device_args)
    if "driver" not in args:
        args["driver"] = "usdr"
    return SoapySDR.Device(args)


def call_list(dev: Any, name: str, *args: Any) -> List[Any]:
    value = getattr(dev, name)(*args)
    return list(value) if value is not None else []


def has_method(dev: Any, name: str) -> bool:
    return callable(getattr(dev, name, None))


def check_identification(runner: Runner, dev: Any) -> Dict[str, Any]:
    info: Dict[str, Any] = {}
    info["driver_key"] = runner.check("getDriverKey", lambda: dev.getDriverKey())
    info["hardware_key"] = runner.check("getHardwareKey", lambda: dev.getHardwareKey())
    info["hardware_info"] = runner.check("getHardwareInfo", lambda: dict(dev.getHardwareInfo()))
    runner.require(info["driver_key"] is not None, "driver key is unavailable")
    runner.report["identification"] = info
    return info


def check_channels(runner: Runner, dev: Any) -> Dict[str, Any]:
    channels: Dict[str, Any] = {}
    for direction, label in DIRECTIONS:
        count = runner.check(f"{label} getNumChannels", lambda d=direction: int(dev.getNumChannels(d)))
        count = int(count or 0)
        channels[label] = {"software_channels": count, "channels": []}
        runner.require(count >= 0, f"{label} channel count is negative")
        for channel in range(count):
            item: Dict[str, Any] = {"channel": channel}
            item["full_duplex"] = runner.check(
                f"{label}{channel} getFullDuplex",
                lambda d=direction, c=channel: bool(dev.getFullDuplex(d, c)),
            )
            if has_method(dev, "getChannelInfo"):
                item["channel_info"] = runner.check(
                    f"{label}{channel} getChannelInfo",
                    lambda d=direction, c=channel: dict(dev.getChannelInfo(d, c)),
                )
            channels[label]["channels"].append(item)
    runner.report["channels"] = channels
    return channels


def check_control_plane(runner: Runner, dev: Any, args: argparse.Namespace) -> None:
    control: Dict[str, Any] = {}
    for direction, label in DIRECTIONS:
        count = int(dev.getNumChannels(direction))
        control[label] = []
        for channel in range(count):
            prefix = f"{label}{channel}"
            item: Dict[str, Any] = {"channel": channel}

            item["antennas"] = runner.check(
                f"{prefix} listAntennas",
                lambda d=direction, c=channel: call_list(dev, "listAntennas", d, c),
            )
            test_antenna = first_manual_antenna(item["antennas"])
            if test_antenna:
                runner.check(
                    f"{prefix} set/getAntenna({test_antenna})",
                    lambda d=direction, c=channel, ant=test_antenna: antenna_roundtrip(dev, d, c, ant),
                )

            item["stream_formats"] = runner.check(
                f"{prefix} getStreamFormats",
                lambda d=direction, c=channel: call_list(dev, "getStreamFormats", d, c),
            )
            runner.require(bool(item["stream_formats"]), f"{prefix} has no stream formats")

            item["native_format"] = runner.check(
                f"{prefix} getNativeStreamFormat",
                lambda d=direction, c=channel: get_native_format(dev, d, c),
            )
            item["stream_args"] = runner.check(
                f"{prefix} getStreamArgsInfo",
                lambda d=direction, c=channel: arg_info_list(dev.getStreamArgsInfo(d, c)),
            )
            item["has_iq_balance_mode"] = runner.check(
                f"{prefix} hasIQBalanceMode",
                lambda d=direction, c=channel: bool(dev.hasIQBalanceMode(d, c)),
            )
            runner.check(
                f"{prefix} set/getIQBalanceMode(false)",
                lambda d=direction, c=channel: iq_balance_mode_roundtrip(dev, d, c, False),
            )
            item["has_frequency_correction"] = runner.check(
                f"{prefix} hasFrequencyCorrection",
                lambda d=direction, c=channel: bool(dev.hasFrequencyCorrection(d, c)),
            )
            runner.check(
                f"{prefix} set/getFrequencyCorrection(0)",
                lambda d=direction, c=channel: frequency_correction_roundtrip(dev, d, c, 0.0),
            )

            item["freq_names"] = runner.check(
                f"{prefix} listFrequencies",
                lambda d=direction, c=channel: call_list(dev, "listFrequencies", d, c),
            )
            item["frequency_range"] = runner.check(
                f"{prefix} getFrequencyRange",
                lambda d=direction, c=channel: ranges_to_json(dev.getFrequencyRange(d, c)),
            )
            runner.require(bool(item["frequency_range"]), f"{prefix} has no frequency range")

            freq_ranges = dev.getFrequencyRange(direction, channel)
            target_freq = pick_in_range(freq_ranges, args.frequency)
            item["test_frequency"] = target_freq
            runner.check(
                f"{prefix} set/getFrequency",
                lambda d=direction, c=channel, f=target_freq: frequency_roundtrip(dev, d, c, f),
            )
            for name in item["freq_names"] or []:
                item[f"frequency_range_{name}"] = runner.check(
                    f"{prefix} getFrequencyRange({name})",
                    lambda d=direction, c=channel, n=name: ranges_to_json(dev.getFrequencyRange(d, c, n)),
                )

            item["sample_rate_range"] = runner.check(
                f"{prefix} getSampleRateRange",
                lambda d=direction, c=channel: ranges_to_json(dev.getSampleRateRange(d, c)),
            )
            sr_ranges = dev.getSampleRateRange(direction, channel)
            target_rate = pick_in_range(sr_ranges, args.sample_rate)
            item["test_sample_rate"] = target_rate
            runner.check(
                f"{prefix} set/getSampleRate",
                lambda d=direction, c=channel, r=target_rate: sample_rate_roundtrip(dev, d, c, r),
            )
            runner.check(
                f"{prefix} listSampleRates",
                lambda d=direction, c=channel: finite_list(dev.listSampleRates(d, c)),
            )
            # print(dev.listSampleRates(direction, channel))

            item["bandwidth_range"] = runner.check(
                f"{prefix} getBandwidthRange",
                lambda d=direction, c=channel: ranges_to_json(dev.getBandwidthRange(d, c)),
            )
            bw_ranges = dev.getBandwidthRange(direction, channel)
            if bw_ranges:
                target_bw = pick_in_range(bw_ranges, args.bandwidth)
                item["test_bandwidth"] = target_bw
                runner.check(
                    f"{prefix} set/getBandwidth",
                    lambda d=direction, c=channel, bw=target_bw: bandwidth_roundtrip(dev, d, c, bw),
                )
            runner.check(
                f"{prefix} listBandwidths",
                lambda d=direction, c=channel: finite_list(dev.listBandwidths(d, c)),
            )
            # print(dev.listBandwidths(direction, channel))

            item["gains"] = runner.check(
                f"{prefix} listGains",
                lambda d=direction, c=channel: call_list(dev, "listGains", d, c),
            )
            item["has_gain_mode"] = runner.check(
                f"{prefix} hasGainMode",
                lambda d=direction, c=channel: bool(dev.hasGainMode(d, c)),
            )
            runner.check(
                f"{prefix} set/getGainMode(false)",
                lambda d=direction, c=channel: gain_mode_roundtrip(dev, d, c, False),
            )
            for gain_name in item["gains"] or []:
                gain_range = runner.check(
                    f"{prefix} getGainRange({gain_name})",
                    lambda d=direction, c=channel, n=gain_name: range_to_json(dev.getGainRange(d, c, n)),
                )
                if gain_range:
                    target_gain = pick_gain(gain_range)
                    runner.check(
                        f"{prefix} set/getGain({gain_name})",
                        lambda d=direction, c=channel, n=gain_name, g=target_gain: gain_roundtrip(dev, d, c, n, g),
                    )
            if item["gains"]:
                runner.check(f"{prefix} getGain overall", lambda d=direction, c=channel: finite_float(dev.getGain(d, c)))

            item["sensors"] = runner.check(
                f"{prefix} listSensors",
                lambda d=direction, c=channel: call_list(dev, "listSensors", d, c),
            )
            for sensor in item["sensors"] or []:
                runner.check(
                    f"{prefix} get/readSensor({sensor})",
                    lambda d=direction, c=channel, s=sensor: sensor_read(dev, d, c, s),
                )

            control[label].append(item)
    runner.report["control"] = control


def antenna_roundtrip(dev: Any, direction: int, channel: int, antenna: str) -> str:
    dev.setAntenna(direction, channel, antenna)
    return str(dev.getAntenna(direction, channel))


def first_manual_antenna(antennas: Any) -> Optional[str]:
    for antenna in antennas or []:
        if str(antenna).upper() != "AUTO":
            return str(antenna)
    return None


def get_native_format(dev: Any, direction: int, channel: int) -> Dict[str, Any]:
    try:
        native = dev.getNativeStreamFormat(direction, channel)
    except TypeError as exc:
        if "fullScale" not in str(exc):
            raise
        candidates: List[Any] = [[0.0]]
        if hasattr(SoapySDR, "SoapySDRDoubleList"):
            candidates.append(SoapySDR.SoapySDRDoubleList([0.0]))
        candidates.append(0.0)

        last_exc: Optional[Exception] = None
        for full_scale in candidates:
            try:
                native = dev.getNativeStreamFormat(direction, channel, full_scale)
                if isinstance(native, tuple):
                    return {"format": native[0], "full_scale": float(native[1])}
                if isinstance(full_scale, list):
                    return {"format": str(native), "full_scale": float(full_scale[0])}
                return {"format": str(native)}
            except Exception as fallback_exc:
                last_exc = fallback_exc
        if last_exc is not None:
            if "double &" in str(last_exc):
                raise SkipCheck("Python binding cannot marshal getNativeStreamFormat fullScale double&")
            raise last_exc
        raise
    if isinstance(native, tuple):
        return {"format": native[0], "full_scale": float(native[1])}
    return {"format": str(native)}


def arg_info_list(infos: Iterable[Any]) -> List[Dict[str, Any]]:
    out = []
    for info in infos:
        out.append(
            {
                "key": getattr(info, "key", ""),
                "name": getattr(info, "name", ""),
                "value": getattr(info, "value", ""),
                "type": str(getattr(info, "type", "")),
            }
        )
    return out


def range_to_json(rng: Any) -> Dict[str, float]:
    return {"minimum": range_min(rng), "maximum": range_max(rng)}


def finite_float(value: Any) -> float:
    out = float(value)
    if not math.isfinite(out):
        raise AssertionError(f"value is not finite: {value}")
    return out


def finite_list(values: Iterable[Any]) -> List[float]:
    out = [finite_float(value) for value in values]
    return out


def sample_rate_roundtrip(dev: Any, direction: int, channel: int, rate: float) -> Dict[str, float]:
    dev.setSampleRate(direction, channel, rate)
    actual = finite_float(dev.getSampleRate(direction, channel))
    if actual <= 0:
        raise AssertionError(f"sample rate readback is not positive: {actual}")
    return {"requested": rate, "actual": actual}


def frequency_roundtrip(dev: Any, direction: int, channel: int, frequency: float) -> Dict[str, float]:
    dev.setFrequency(direction, channel, frequency)
    actual = finite_float(dev.getFrequency(direction, channel))
    if actual <= 0:
        raise AssertionError(f"frequency readback is not positive: {actual}")
    return {"requested": frequency, "actual": actual}


def bandwidth_roundtrip(dev: Any, direction: int, channel: int, bandwidth: float) -> Dict[str, float]:
    dev.setBandwidth(direction, channel, bandwidth)
    actual = finite_float(dev.getBandwidth(direction, channel))
    if actual <= 0:
        raise AssertionError(f"bandwidth readback is not positive: {actual}")
    return {"requested": bandwidth, "actual": actual}


def pick_gain(gain_range: Dict[str, float]) -> float:
    lo = gain_range["minimum"]
    hi = gain_range["maximum"]
    if not math.isfinite(lo) or not math.isfinite(hi):
        return 0.0
    if hi <= lo:
        return lo
    return lo + (hi - lo) * 0.5


def gain_roundtrip(dev: Any, direction: int, channel: int, name: str, gain: float) -> Dict[str, float]:
    dev.setGain(direction, channel, name, gain)
    actual = finite_float(dev.getGain(direction, channel, name))
    return {"requested": gain, "actual": actual}


def gain_mode_roundtrip(dev: Any, direction: int, channel: int, automatic: bool) -> Dict[str, bool]:
    dev.setGainMode(direction, channel, automatic)
    actual = bool(dev.getGainMode(direction, channel))
    if actual != automatic:
        raise AssertionError(f"gain mode readback mismatch: requested={automatic} actual={actual}")
    return {"requested": automatic, "actual": actual}


def iq_balance_mode_roundtrip(dev: Any, direction: int, channel: int, automatic: bool) -> Dict[str, bool]:
    dev.setIQBalanceMode(direction, channel, automatic)
    actual = bool(dev.getIQBalanceMode(direction, channel))
    if actual != automatic:
        raise AssertionError(f"IQ balance mode readback mismatch: requested={automatic} actual={actual}")
    return {"requested": automatic, "actual": actual}


def frequency_correction_roundtrip(dev: Any, direction: int, channel: int, value: float) -> Dict[str, float]:
    dev.setFrequencyCorrection(direction, channel, value)
    actual = finite_float(dev.getFrequencyCorrection(direction, channel))
    if actual != value:
        raise AssertionError(f"frequency correction readback mismatch: requested={value} actual={actual}")
    return {"requested": value, "actual": actual}


def sensor_read(dev: Any, direction: int, channel: int, sensor: str) -> Dict[str, str]:
    info = dev.getSensorInfo(direction, channel, sensor)
    value = dev.readSensor(direction, channel, sensor)
    result = {"key": getattr(info, "key", sensor), "name": getattr(info, "name", ""), "value": str(value)}
    print(f"  Sensor {result['key']} {result['name']} value={result['value']}")
    return result


def check_global_functions(runner: Runner, dev: Any) -> None:
    global_info: Dict[str, Any] = {}
    global_info["clock_sources"] = runner.check("listClockSources", lambda: call_list(dev, "listClockSources"))
    if global_info["clock_sources"]:
        source = global_info["clock_sources"][0]
        runner.check("set/getClockSource first option", lambda s=source: clock_source_roundtrip(dev, s))
    global_info["master_clock_rates"] = runner.check(
        "getMasterClockRates",
        lambda: ranges_to_json(dev.getMasterClockRates()),
    )
    global_info["reference_clock_rates"] = runner.check(
        "getReferenceClockRates",
        lambda: ranges_to_json(dev.getReferenceClockRates()),
    )
    reference_clock_rate = runner.check("getReferenceClockRate", lambda: finite_float(dev.getReferenceClockRate()))
    if reference_clock_rate and reference_clock_rate > 0:
        runner.check("set/getReferenceClockRate current", lambda r=reference_clock_rate: reference_clock_roundtrip(dev, r))
    else:
        runner.skip("set/getReferenceClockRate current", "current reference clock rate is unavailable")
    if has_method(dev, "getNativeDeviceHandle"):
        runner.check("getNativeDeviceHandle", lambda: str(dev.getNativeDeviceHandle()))
    global_info["time_sources"] = runner.check("listTimeSources", lambda: call_list(dev, "listTimeSources"))
    runner.check("hasHardwareTime", lambda: bool(dev.hasHardwareTime()))
    runner.check("getHardwareTime", lambda: int(dev.getHardwareTime()))
    global_info["sensors"] = runner.check("listSensors", lambda: call_list(dev, "listSensors"))
    for sensor in global_info["sensors"] or []:
        runner.check(f"get/readSensor({sensor})", lambda s=sensor: global_sensor_read(dev, s))
    runner.report["global"] = global_info


def clock_source_roundtrip(dev: Any, source: str) -> str:
    dev.setClockSource(source)
    return str(dev.getClockSource())


def reference_clock_roundtrip(dev: Any, rate: float) -> Dict[str, float]:
    dev.setReferenceClockRate(rate)
    actual = finite_float(dev.getReferenceClockRate())
    return {"requested": rate, "actual": actual}


def global_sensor_read(dev: Any, sensor: str) -> Dict[str, str]:
    info = dev.getSensorInfo(sensor)
    value = dev.readSensor(sensor)
    result = {"key": getattr(info, "key", sensor), "name": getattr(info, "name", ""), "value": str(value)}
    print(f"Sensor {result['key']} {result['name']} value={result['value']}")
    return result


def check_rx_stream(runner: Runner, dev: Any, args: argparse.Namespace) -> None:
    rx_channels = int(dev.getNumChannels(SOAPY_SDR_RX))
    if rx_channels <= 0:
        runner.skip("RX stream", "device has no RX channels")
        return

    try:
        import numpy as np
    except Exception as exc:
        runner.skip("RX stream", f"numpy is not available: {exc}")
        return

    channel = min(args.rx_channel, rx_channels - 1)
    sr_ranges = dev.getSampleRateRange(SOAPY_SDR_RX, channel)
    freq_ranges = dev.getFrequencyRange(SOAPY_SDR_RX, channel)
    bw_ranges = dev.getBandwidthRange(SOAPY_SDR_RX, channel)
    sample_rate = pick_in_range(sr_ranges, args.sample_rate)
    frequency = pick_in_range(freq_ranges, args.frequency)
    bandwidth = pick_in_range(bw_ranges, args.bandwidth) if bw_ranges else 0.0

    def parse_sample_sizes(value: str) -> List[int]:
        sizes: List[int] = []
        for item in value.split(","):
            item = item.strip()
            if not item:
                continue
            size = int(item)
            if size <= 0:
                raise ValueError(f"stream read size should be positive: {size}")
            sizes.append(size)
        return sizes

    def check_next_timestamp(prev: Dict[str, Any], item: Dict[str, Any]) -> None:
        if prev["timeNs"] == 0 or item["timeNs"] == 0:
            return
        delta_samples = round((item["timeNs"] - prev["timeNs"]) * sample_rate / 1e9)
        if abs(delta_samples - prev["ret"]) > 1:
            raise AssertionError(
                f"timestamp step mismatch: expected {prev['ret']} samples, got {delta_samples}"
            )

    def run_stream() -> Dict[str, Any]:
        dev.setSampleRate(SOAPY_SDR_RX, channel, sample_rate)
        dev.setFrequency(SOAPY_SDR_RX, channel, frequency)
        if bandwidth > 0:
            dev.setBandwidth(SOAPY_SDR_RX, channel, bandwidth)

        stream_args = {
            "bufferLength": str(args.rx_samples),
            "linkFormat": args.link_format,
            "rxGapFill": args.rx_gap_fill,
        }
        stream = dev.setupStream(SOAPY_SDR_RX, "CF32", [channel], stream_args)
        reads: List[Dict[str, Any]] = []
        variable_reads: List[Dict[str, Any]] = []
        variable_sizes = parse_sample_sizes(args.rx_variable_sizes)
        max_read_size = max([args.rx_samples] + variable_sizes)
        try:
            mtu = int(dev.getStreamMTU(stream))
            inactive = dev.readStream(stream, [np.empty(args.rx_samples, np.complex64)], args.rx_samples, timeoutUs=10000)
            dev.activateStream(stream)
            for _ in range(args.rx_reads):
                buff = np.empty(args.rx_samples, np.complex64)
                result = dev.readStream(stream, [buff], args.rx_samples, timeoutUs=args.timeout_ms * 1000)
                ret = int(getattr(result, "ret", result))
                if ret <= 0:
                    raise AssertionError(f"readStream returned {ret}")
                reads.append(
                    {
                        "ret": ret,
                        "flags": int(getattr(result, "flags", 0)),
                        "timeNs": int(getattr(result, "timeNs", 0)),
                        "mean_abs": float(np.mean(np.abs(buff[:ret]))),
                    }
                )
            prev_read: Optional[Dict[str, Any]] = reads[-1] if reads else None
            for requested in variable_sizes:
                buff = np.empty(max_read_size, np.complex64)
                result = dev.readStream(stream, [buff], requested, timeoutUs=args.timeout_ms * 1000)
                ret = int(getattr(result, "ret", result))
                if ret != requested:
                    raise AssertionError(f"readStream requested {requested}, returned {ret}")
                item = {
                    "requested": requested,
                    "ret": ret,
                    "flags": int(getattr(result, "flags", 0)),
                    "timeNs": int(getattr(result, "timeNs", 0)),
                    "mean_abs": float(np.mean(np.abs(buff[:ret]))),
                }
                if prev_read is not None:
                    check_next_timestamp(prev_read, item)
                variable_reads.append(item)
                prev_read = item
            dev.deactivateStream(stream)
            return {
                "channel": channel,
                "mtu": mtu,
                "inactive_read_ret": int(getattr(inactive, "ret", inactive)),
                "reads": reads,
                "variable_reads": variable_reads,
            }
        finally:
            dev.closeStream(stream)

    runner.report["rx_stream"] = runner.check("RX setup/activate/read/deactivate/close stream", run_stream)


def check_tx_stream(runner: Runner, dev: Any, args: argparse.Namespace) -> None:
    tx_channels = int(dev.getNumChannels(SOAPY_SDR_TX))
    if tx_channels <= 0:
        runner.skip("TX stream", "device has no TX channels")
        return

    try:
        import numpy as np
    except Exception as exc:
        runner.skip("TX stream", f"numpy is not available: {exc}")
        return

    channel = min(args.tx_channel, tx_channels - 1)
    sample_rate = pick_in_range(dev.getSampleRateRange(SOAPY_SDR_TX, channel), args.sample_rate)
    frequency = pick_in_range(dev.getFrequencyRange(SOAPY_SDR_TX, channel), args.frequency)

    def run_stream() -> Dict[str, Any]:
        dev.setSampleRate(SOAPY_SDR_TX, channel, sample_rate)
        dev.setFrequency(SOAPY_SDR_TX, channel, frequency)
        stream = dev.setupStream(SOAPY_SDR_TX, "CF32", [channel], {"bufferLength": str(args.tx_packet_samples)})
        active = False
        try:
            mtu = int(dev.getStreamMTU(stream))
            write_elems = max(args.tx_samples, mtu * 2)
            buff = np.zeros(write_elems, np.complex64)
            dev.activateStream(stream)
            active = True
            result = dev.writeStream(stream, [buff], write_elems, timeoutUs=args.timeout_ms * 1000)
            ret = int(getattr(result, "ret", result))
            dev.deactivateStream(stream)
            active = False
            if ret != write_elems:
                raise AssertionError(f"writeStream requested {write_elems}, returned {ret}")
            return {"channel": channel, "mtu": mtu, "requested": write_elems, "ret": ret}
        finally:
            if active:
                dev.deactivateStream(stream)
            dev.closeStream(stream)

    runner.report["tx_stream"] = runner.check("TX large writeStream chunking", run_stream)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="driver=usdr", help="SoapySDR device string")
    parser.add_argument("--require-device", action="store_true", help="Fail instead of skip when no matching device is found")
    parser.add_argument("--sample-rate", type=float, default=5e6, help="Preferred safe sample rate")
    parser.add_argument("--frequency", type=float, default=912.3e6, help="Preferred safe RF frequency")
    parser.add_argument("--bandwidth", type=float, default=1e6, help="Preferred safe bandwidth")
    parser.add_argument("--rx-stream", action="store_true", help="Run RX streaming smoke test")
    parser.add_argument("--rx-channel", type=int, default=0, help="RX channel to stream")
    parser.add_argument("--rx-samples", type=int, default=4096, help="Samples per RX read")
    parser.add_argument("--rx-reads", type=int, default=4, help="Number of RX reads")
    parser.add_argument(
        "--rx-variable-sizes",
        default="17,1024,4095,4096,4097,8193",
        help="Comma-separated RX read sizes used to test packet buffering",
    )
    parser.add_argument("--rx-gap-fill", default="none", choices=("none", "zero"), help="RX timestamp gap fill mode")
    parser.add_argument("--tx-stream", action="store_true", help="Run TX writeStream chunking smoke test")
    parser.add_argument("--tx-channel", type=int, default=0, help="TX channel to stream")
    parser.add_argument("--tx-packet-samples", type=int, default=4096, help="Hardware packet size for TX stream setup")
    parser.add_argument("--tx-samples", type=int, default=8192, help="Minimum TX samples to write in one large call")
    parser.add_argument("--link-format", default="CS16", choices=("CS16", "CS12"), help="RX link format")
    parser.add_argument("--timeout-ms", type=int, default=1000, help="Stream timeout")
    parser.add_argument("--json", dest="json_path", default="", help="Optional JSON report path")
    parser.add_argument("--verbose", action="store_true", help="Print more details")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    runner = Runner(verbose=args.verbose)
    device_args = setting_from_string(args.device)

    print(f"Device args: {device_args}")
    devices = runner.check("enumerate", lambda: enumerate_devices(device_args)) or []
    runner.report["enumerate"] = devices
    if not devices:
        message = "no matching SoapySDR USDR devices were found"
        if args.require_device:
            runner.failures.append(message)
            print(f"FAIL {message}")
            return finish(runner, args)
        runner.skip("device open", message)
        return finish(runner, args)

    dev = runner.check("make device", lambda: make_device(device_args))
    if dev is None:
        return finish(runner, args)

    try:
        check_identification(runner, dev)
        check_channels(runner, dev)
        check_global_functions(runner, dev)
        check_control_plane(runner, dev, args)
        if args.rx_stream:
            check_rx_stream(runner, dev, args)
        if args.tx_stream:
            check_tx_stream(runner, dev, args)
    finally:
        if hasattr(dev, "close"):
            dev.close()
        dev = None

    return finish(runner, args)


def finish(runner: Runner, args: argparse.Namespace) -> int:
    runner.report["summary"] = {
        "failures": runner.failures,
        "skips": runner.skips,
        "timestamp": time.time(),
    }
    if args.json_path:
        with open(args.json_path, "w", encoding="utf-8") as stream:
            json.dump(runner.report, stream, indent=2, sort_keys=True)
            stream.write("\n")

    print("")
    print(f"Summary: {len(runner.failures)} failed, {len(runner.skips)} skipped")
    if runner.failures:
        for failure in runner.failures:
            print(f"  {failure}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
