"""Minimal ctypes bindings and high-level Pythonic wrappers for libusdr."""

from __future__ import annotations

import ctypes
import errno
import ctypes.util
from dataclasses import dataclass
from typing import Dict, Iterable, Optional, Sequence, Tuple, Union


class UsdrException(RuntimeError):
    """Raised by high-level wrappers when a libusdr operation fails."""

    def __init__(self, fn_name: str, code: int):
        super().__init__(f"{fn_name} failed with error code {code}")
        self.fn_name = fn_name
        self.code = code


# Backward-compatible alias from previous revisions.
USDRError = UsdrException


class UsdrDmsNfo(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_uint),
        ("channels", ctypes.c_uint),
        ("pktbszie", ctypes.c_uint),
        ("pktsyms", ctypes.c_uint),
        ("totsamptick", ctypes.c_uint),
        ("burst_count", ctypes.c_uint),
    ]


class UsdrChannelInfo(ctypes.Structure):
    _fields_ = [
        ("count", ctypes.c_uint),
        ("flags", ctypes.c_uint),
        ("phys_names", ctypes.POINTER(ctypes.c_char_p)),
        ("phys_nums", ctypes.POINTER(ctypes.c_uint)),
    ]


class UsdrDmsRecvNfo(ctypes.Structure):
    _fields_ = [
        ("fsymtime", ctypes.c_uint64),
        ("totsyms", ctypes.c_uint),
        ("totlost", ctypes.c_uint),
        ("max_parts", ctypes.c_uint),
        ("extra", ctypes.c_uint64),
    ]


@dataclass(frozen=True)
class UsdrStreamInfo:
    """Pure-Python stream metadata object returned by :meth:`UsdrStream.info`."""

    type: int
    channels: int
    pktbszie: int
    pktsyms: int
    totsamptick: int
    burst_count: int

    @classmethod
    def from_ctypes(cls, nfo: "UsdrDmsNfo") -> "UsdrStreamInfo":
        return cls(
            type=int(nfo.type),
            channels=int(nfo.channels),
            pktbszie=int(nfo.pktbszie),
            pktsyms=int(nfo.pktsyms),
            totsamptick=int(nfo.totsamptick),
            burst_count=int(nfo.burst_count),
        )

    def __str__(self) -> str:
        return (
            f"UsdrStreamInfo(type={self.type}, channels={self.channels}, "
            f"pktbytes={self.pktbszie}, pktsyms={self.pktsyms}, "
            f"totsamptick={self.totsamptick}, burst_count={self.burst_count})"
        )


USDR_DMS_START = 0
USDR_DMS_STOP = 1
USDR_DMS_START_AT = 2
USDR_DMS_STOP_AT = 3

ChannelId = Union[int, str]
ChannelMap = Optional[Sequence[ChannelId]]


def empty_aligned_complex64(shape, alignment=64):
    """
    Creates an uninitialized complex64 array aligned to the specified byte boundary.
    """
    import numpy as np

    dtype = np.dtype(np.complex64)
    # Calculate total bytes needed
    n_bytes = np.prod(shape) * dtype.itemsize

    # Allocate extra 'alignment' bytes to ensure we can find an aligned start
    raw_buffer = np.empty(n_bytes + alignment, dtype=np.uint8)

    # Find the starting address and calculate the offset to the next 64-byte boundary
    start_address = raw_buffer.ctypes.data
    offset = (alignment - (start_address % alignment)) % alignment

    # Create the view starting at the aligned offset
    aligned_array = raw_buffer[offset : offset + n_bytes].view(dtype).reshape(shape)
    return aligned_array


class UsdrLib:
    """Thin low-level wrapper around selected libusdr C APIs (non-raising)."""

    def __init__(self, library_path: Optional[str] = None) -> None:
        candidates = [
            library_path,
            ctypes.util.find_library("usdr"),
            "libusdr.so",
            "libusdr.dylib",
            "usdr.dll",
        ]

        last_exc = None
        self._lib = None
        self._stream_meta: Dict[int, Tuple[str, UsdrDmsNfo]] = {}
        for name in candidates:
            if not name:
                continue
            try:
                self._lib = ctypes.CDLL(name)
                break
            except OSError as exc:
                last_exc = exc

        if self._lib is None:
            raise OSError(f"Unable to load libusdr: {last_exc}")

        self._configure_signatures()

    def _configure_signatures(self) -> None:
        self._lib.usdr_dmd_create_string.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        self._lib.usdr_dmd_create_string.restype = ctypes.c_int

        self._lib.usdr_dmd_close.argtypes = [ctypes.c_void_p]
        self._lib.usdr_dmd_close.restype = ctypes.c_int

        self._lib.usdr_dme_set_uint.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64]
        self._lib.usdr_dme_set_uint.restype = ctypes.c_int

        self._lib.usdr_dme_set_string.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        self._lib.usdr_dme_set_string.restype = ctypes.c_int

        self._lib.usdr_dme_get_uint.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint64)]
        self._lib.usdr_dme_get_uint.restype = ctypes.c_int

        self._lib.usdr_dme_get_u32.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint32)]
        self._lib.usdr_dme_get_u32.restype = ctypes.c_int

        self._lib.usdr_dmr_rate_set.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint]
        self._lib.usdr_dmr_rate_set.restype = ctypes.c_int

        self._lib.usdr_dms_create_ex2.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(UsdrChannelInfo),
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_void_p),
        ]
        self._lib.usdr_dms_create_ex2.restype = ctypes.c_int

        self._lib.usdr_dms_destroy.argtypes = [ctypes.c_void_p]
        self._lib.usdr_dms_destroy.restype = ctypes.c_int

        self._lib.usdr_dms_info.argtypes = [ctypes.c_void_p, ctypes.POINTER(UsdrDmsNfo)]
        self._lib.usdr_dms_info.restype = ctypes.c_int

        self._lib.usdr_dms_op.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint64]
        self._lib.usdr_dms_op.restype = ctypes.c_int

        self._lib.usdr_dms_sync.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
        self._lib.usdr_dms_sync.restype = ctypes.c_int

        self._lib.usdr_dms_recv.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.c_uint,
            ctypes.POINTER(UsdrDmsRecvNfo),
        ]
        self._lib.usdr_dms_recv.restype = ctypes.c_int

        self._lib.usdr_dms_send.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.c_uint,
            ctypes.c_uint64,
            ctypes.c_uint,
        ]
        self._lib.usdr_dms_send.restype = ctypes.c_int

    @staticmethod
    def _normalize_host_format(dformat: str) -> str:
        host_fmt = dformat.split("@", 1)[0]
        host_fmt = host_fmt.split(";", 1)[0]
        return host_fmt.strip().lower()

    def _stream_key(self, stream: ctypes.c_void_p) -> int:
        return int(ctypes.cast(stream, ctypes.c_void_p).value)

    @staticmethod
    def _validate_channels(channels: ChannelMap) -> Tuple[int, Sequence[ChannelId]]:
        if channels is None:
            return 0, [0]
        if not isinstance(channels, Sequence) or isinstance(channels, (str, bytes)):
            return -errno.EINVAL, []
        if len(channels) == 0:
            return -errno.EINVAL, []

        all_int = all(isinstance(ch, int) and not isinstance(ch, bool) for ch in channels)
        all_str = all(isinstance(ch, str) for ch in channels)
        if not (all_int or all_str):
            return -errno.EINVAL, []

        return 0, channels

    # Low-level methods return integer status codes (0 on success).
    def usdrlog_setlevel(self, loglevel: int, subsystem: Optional[str] = None) -> None:
        """Set libusdr logging level for a subsystem or default (None)."""
        self._lib.usdrlog_setlevel(subsystem.encode("utf-8") if subsystem else None, int(loglevel))

    # Low-level methods return integer status codes (0 on success).
    def usdr_dmd_create_string(self, connection_string: str) -> Tuple[int, ctypes.c_void_p]:
        """Create device handle from a connection string.

        Returns:
            Tuple of ``(res, dev_handle)`` where ``res == 0`` on success.
        """
        dev = ctypes.c_void_p()
        res = self._lib.usdr_dmd_create_string(connection_string.encode("utf-8"), ctypes.byref(dev))
        return int(res), dev

    def usdr_dmd_close(self, device: ctypes.c_void_p) -> int:
        """Close device handle previously created by :meth:`usdr_dmd_create_string`."""
        return int(self._lib.usdr_dmd_close(device))

    def usdr_dme_set_uint(self, device: ctypes.c_void_p, path: str, value: int) -> int:
        """Set unsigned integer parameter by full path."""
        return int(self._lib.usdr_dme_set_uint(device, path.encode("utf-8"), int(value)))

    def usdr_dme_set_string(self, device: ctypes.c_void_p, path: str, value: str) -> int:
        """Set string parameter by full path."""
        return int(self._lib.usdr_dme_set_string(device, path.encode("utf-8"), value.encode("utf-8")))

    def usdr_dme_get_uint(self, device: ctypes.c_void_p, path: str) -> Tuple[int, int]:
        """Read unsigned integer parameter by full path.

        Returns:
            ``(res, value)`` where value is valid when ``res == 0``.
        """
        out = ctypes.c_uint64()
        res = self._lib.usdr_dme_get_uint(device, path.encode("utf-8"), ctypes.byref(out))
        return int(res), int(out.value)

    def usdr_dme_get_u32(self, device: ctypes.c_void_p, path: str) -> Tuple[int, int]:
        """Read 32-bit unsigned parameter by full path."""
        out = ctypes.c_uint32()
        res = self._lib.usdr_dme_get_u32(device, path.encode("utf-8"), ctypes.byref(out))
        return int(res), int(out.value)

    def usdr_dmr_rate_set(self, device: ctypes.c_void_p, rate: int, rate_name: Optional[str] = None) -> int:
        """Set sample rate for device (master rate when ``rate_name`` is ``None``)."""
        return int(self._lib.usdr_dmr_rate_set(device, rate_name.encode("utf-8") if rate_name else None, int(rate)))

    def usdr_dms_create_ex2(
        self,
        device: ctypes.c_void_p,
        sobj: str,
        dformat: str = "cf32",
        channels: ChannelMap = None,
        pktsyms: int = 4096,
        flags: int = 0,
        parameters: Optional[str] = None,
    ) -> Tuple[int, ctypes.c_void_p]:
        """Create stream handle with explicit channel mapping and format.

        ``channels`` may be either a list of integers (mapped to ``phys_nums``) or a
        list of strings (mapped to ``phys_names``). If omitted, ``[0]`` is used.

        Returns:
            ``(res, stream_handle)`` where ``res == 0`` on success.
        """
        vres, channels = self._validate_channels(channels)
        if vres != 0:
            return vres, ctypes.c_void_p()

        chan_info = UsdrChannelInfo(count=len(channels), flags=0, phys_names=None, phys_nums=None)

        chan_nums = None
        chan_names_arr = None
        chan_names_storage = None
        if isinstance(channels[0], int):
            chan_nums = (ctypes.c_uint * len(channels))(*[int(ch) for ch in channels])
            chan_info.phys_nums = chan_nums
        else:
            chan_names_storage = [ch.encode("utf-8") for ch in channels]
            chan_names_arr = (ctypes.c_char_p * len(channels))(*chan_names_storage)
            chan_info.phys_names = chan_names_arr

        out_stream = ctypes.c_void_p()
        res = self._lib.usdr_dms_create_ex2(
            device,
            sobj.encode("utf-8"),
            dformat.encode("utf-8"),
            ctypes.byref(chan_info),
            pktsyms,
            flags,
            parameters.encode("utf-8") if parameters is not None else None,
            ctypes.byref(out_stream),
        )

        if res == 0:
            info_res, stream_info = self.usdr_dms_info(out_stream)
            if info_res == 0:
                self._stream_meta[self._stream_key(out_stream)] = (self._normalize_host_format(dformat), stream_info)
        return int(res), out_stream

    def usdr_dms_destroy(self, stream: ctypes.c_void_p) -> int:
        """Destroy stream handle."""
        res = int(self._lib.usdr_dms_destroy(stream))
        if res == 0:
            self._stream_meta.pop(self._stream_key(stream), None)
        return res

    def usdr_dms_info(self, stream: ctypes.c_void_p) -> Tuple[int, UsdrDmsNfo]:
        """Get low-level stream information struct."""
        info = UsdrDmsNfo()
        res = self._lib.usdr_dms_info(stream, ctypes.byref(info))
        return int(res), info

    def usdr_dms_op(self, stream: ctypes.c_void_p, command: int, tm: int = 0) -> int:
        """Perform stream operation (`START`, `STOP`, timed variants)."""
        return int(self._lib.usdr_dms_op(stream, command, tm))

    def usdr_dms_sync(self, device: ctypes.c_void_p, synctype: str, streams: Iterable[ctypes.c_void_p]) -> int:
        """Synchronize one or more streams using a named sync policy."""
        stream_list = list(streams)
        arr = (ctypes.c_void_p * len(stream_list))(*stream_list)
        return int(self._lib.usdr_dms_sync(device, synctype.encode("utf-8"), len(stream_list), arr))

    def usdr_dms_recv(
        self,
        stream: ctypes.c_void_p,
        buffers: Sequence[ctypes.Array],
        timeout_ms: int,
        with_info: bool = True,
    ) -> Tuple[int, Optional[UsdrDmsRecvNfo]]:
        """Receive into caller-provided ctypes buffers."""
        c_bufs = (ctypes.c_void_p * len(buffers))(*(ctypes.addressof(b) for b in buffers))
        print(c_bufs)
        rx_nfo = UsdrDmsRecvNfo() if with_info else None
        res = self._lib.usdr_dms_recv(stream, c_bufs, timeout_ms, ctypes.byref(rx_nfo) if rx_nfo else None)
        return int(res), rx_nfo

    def usdr_dms_recv_numpy(self, stream: ctypes.c_void_p, timeout_ms: int, with_info: bool = True):
        """Receive into numpy arrays based on stream host format metadata.

        Returns:
            ``(res, arrays, rx_info)``. ``arrays`` is ``None`` when ``res != 0``.
        """
        import numpy as np

        meta = self._stream_meta.get(self._stream_key(stream))
        if meta is None:
            return -errno.EINVAL, None, None

        host_fmt, stream_info = meta
        if host_fmt not in ("ci16", "cf32"):
            return -errno.EOPNOTSUPP, None, None

        out = [ empty_aligned_complex64(stream_info.pktsyms) if host_fmt == "cf32" else np.empty((stream_info.pktsyms, 2), dtype=np.int16) for _ in range(stream_info.channels)]
        raw_buffers_t = ctypes.c_void_p * stream_info.channels
        raw_buffers = raw_buffers_t()
        for i in range(stream_info.channels):
            raw_buffers[i] = out[i].ctypes.data_as(ctypes.c_void_p)

        rx_nfo = UsdrDmsRecvNfo() if with_info else None
        res  = self._lib.usdr_dms_recv(stream, raw_buffers, timeout_ms, ctypes.byref(rx_nfo) if rx_nfo else None)
        if res != 0:
            return res, None, rx_nfo

        return 0, out, rx_nfo

    def usdr_dms_send(self, stream: ctypes.c_void_p, buffers: Sequence[ctypes.Array], samples: int, timestamp: int, timeout_ms: int) -> int:
        """Send from caller-provided ctypes buffers."""
        c_bufs = (ctypes.c_void_p * len(buffers))(*(ctypes.addressof(b) for b in buffers))
        return int(self._lib.usdr_dms_send(stream, c_bufs, samples, timestamp, timeout_ms))

    def usdr_dms_send_numpy(self, stream: ctypes.c_void_p, buffers, timestamp: int, timeout_ms: int, samples: Optional[int] = None) -> int:
        """Send from numpy arrays validated against stream host format metadata."""
        import numpy as np

        meta = self._stream_meta.get(self._stream_key(stream))
        if meta is None:
            return -errno.EINVAL

        host_fmt, stream_info = meta
        if host_fmt not in ("ci16", "cf32"):
            return -errno.EOPNOTSUPP
        if len(buffers) != stream_info.channels:
            return -errno.EINVAL

        np_buffers = []
        inferred_samples = None
        for buf in buffers:
            arr = np.asarray(buf)
            if host_fmt == "ci16":
                if arr.dtype != np.int16 or arr.ndim != 2 or arr.shape[1] != 2:
                    return -errno.EINVAL
                n = arr.shape[0]
            else:
                if arr.dtype != np.complex64 or arr.ndim != 1:
                    return -errno.EINVAL
                n = arr.shape[0]
            if inferred_samples is None:
                inferred_samples = int(n)
            elif int(n) != inferred_samples:
                return -errno.EINVAL
            np_buffers.append(np.ascontiguousarray(arr))

        send_samples = inferred_samples if samples is None else int(samples)
        c_bufs = (ctypes.c_void_p * len(np_buffers))(*(ctypes.c_void_p(arr.ctypes.data) for arr in np_buffers))
        return int(self._lib.usdr_dms_send(stream, c_bufs, send_samples, timestamp, timeout_ms))


class UsdrDevice:
    """Create device wrapper and optionally set global libusdr log level.
    Args:
        device_string: Connection string passed to ``usdr_dmd_create_string``.
        loglevel: Optional log level passed to ``usdrlog_setlevel(NULL, loglevel)`` before opening device.
        lib: Optional pre-created low-level wrapper.
    """
    def __init__(self, device_string: str = "", loglevel: Optional[int] = None, lib: Optional[UsdrLib] = None) -> None:
        self._lib = lib or UsdrLib()
        if loglevel is not None:
            self._lib.usdrlog_setlevel(loglevel)
        self._closed = True
        res, dev = self._lib.usdr_dmd_create_string(device_string)
        if res != 0:
            raise UsdrException("usdr_dmd_create_string", res)
        self._dev = dev
        self._closed = False

    def __enter__(self) -> "UsdrDevice":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    @property
    def handle(self) -> ctypes.c_void_p:
        return self._dev

    @staticmethod
    def _raise_if_error(fn_name: str, code: int) -> None:
        if code != 0:
            raise UsdrException(fn_name, code)

    def _ensure_open(self) -> None:
        if self._closed:
            raise UsdrException("device_closed", -errno.EBADF)

    def close(self) -> None:
        """Close device and release underlying low-level handle."""
        if not self._closed:
            res = self._lib.usdr_dmd_close(self._dev)
            self._raise_if_error("usdr_dmd_close", res)
            self._closed = True

    def set_uint(self, path: str, value: int) -> None:
        """Set arbitrary unsigned integer parameter by full path."""
        self._ensure_open()
        res = self._lib.usdr_dme_set_uint(self._dev, path, value)
        self._raise_if_error("usdr_dme_set_uint", res)

    def set_string(self, path: str, value: str) -> None:
        """Set arbitrary string parameter by full path."""
        self._ensure_open()
        res = self._lib.usdr_dme_set_string(self._dev, path, value)
        self._raise_if_error("usdr_dme_set_string", res)

    def get_uint(self, path: str) -> int:
        """Get arbitrary unsigned integer parameter by full path."""
        self._ensure_open()
        res, value = self._lib.usdr_dme_get_uint(self._dev, path)
        self._raise_if_error("usdr_dme_get_uint", res)
        return value

    def get_u32(self, path: str) -> int:
        """Get arbitrary 32-bit unsigned parameter by full path."""
        self._ensure_open()
        res, value = self._lib.usdr_dme_get_u32(self._dev, path)
        self._raise_if_error("usdr_dme_get_u32", res)
        return value

    def set_samplerate(self, rate: int, rate_name: Optional[str] = None) -> None:
        """Set device sample rate in samples per second.

        Mirrors `usdr_dm_create` behavior (`usdr_dmr_rate_set(dev, NULL, rate)`) when
        `rate_name` is omitted.

        Args:
            rate: Target sample rate in SPS (e.g. 50_000_000).
            rate_name: Optional named rate domain; use ``None`` for master rate.
        """
        self._ensure_open()
        res = self._lib.usdr_dmr_rate_set(self._dev, rate=rate, rate_name=rate_name)
        self._raise_if_error("usdr_dmr_rate_set", res)

    def _set_sdr0_uint(self, endpoint: str, value: int) -> None:
        self.set_uint(f"/dm/sdr/0/{endpoint}", value)

    def _set_sdr0_string(self, endpoint: str, value: str) -> None:
        self.set_string(f"/dm/sdr/0/{endpoint}", value)

    def set_rx_frequency(self, hz: int) -> None:
        """Set RX LO frequency in Hz (`/dm/sdr/0/rx/freqency`)."""
        self._set_sdr0_uint("rx/freqency", hz)

    def set_tx_frequency(self, hz: int) -> None:
        """Set TX LO frequency in Hz (`/dm/sdr/0/tx/freqency`)."""
        self._set_sdr0_uint("tx/freqency", hz)

    def set_tdd_frequency(self, hz: int) -> None:
        """Set TDD frequency in Hz (`/dm/sdr/0/tdd/freqency`)."""
        self._set_sdr0_uint("tdd/freqency", hz)

    def set_rx_bandwidth(self, hz: int) -> None:
        """Set RX bandwidth in Hz (`/dm/sdr/0/rx/bandwidth`)."""
        self._set_sdr0_uint("rx/bandwidth", hz)

    def set_tx_bandwidth(self, hz: int) -> None:
        """Set TX bandwidth in Hz (`/dm/sdr/0/tx/bandwidth`)."""
        self._set_sdr0_uint("tx/bandwidth", hz)

    def set_rx_gain_lna(self, gain_db: int) -> None:
        """Set RX LNA gain (`/dm/sdr/0/rx/gain/lna`)."""
        self._set_sdr0_uint("rx/gain/lna", gain_db)

    def set_rx_gain_vga(self, gain_db: int) -> None:
        """Set RX VGA gain (`/dm/sdr/0/rx/gain/vga`)."""
        self._set_sdr0_uint("rx/gain/vga", gain_db)

    def set_rx_gain_pga(self, gain_db: int) -> None:
        """Set RX PGA gain (`/dm/sdr/0/rx/gain/pga`)."""
        self._set_sdr0_uint("rx/gain/pga", gain_db)

    def set_tx_gain(self, gain_db: int) -> None:
        """Set TX gain (`/dm/sdr/0/tx/gain`)."""
        self._set_sdr0_uint("tx/gain", gain_db)

    def set_rx_path(self, path: str) -> None:
        """Set RX path (`/dm/sdr/0/rx/path`).

        Typical values from `usdr_dm_create`: `rx_auto`, `rxl`, `rxw`, `rxh`, `adc`,
        `rxl_lb`, `rxw_lb`, `rxh_lb`.
        """
        self._set_sdr0_string("rx/path", path)

    def set_tx_path(self, path: str) -> None:
        """Set TX path (`/dm/sdr/0/tx/path`).

        Typical values from `usdr_dm_create`: `tx_auto`, `txb1`, `txb2`, `txw`, `txh`.
        """
        self._set_sdr0_string("tx/path", path)

    def configure_rf(
        self,
        *,
        rx_freq: Optional[int] = None,
        tx_freq: Optional[int] = None,
        tdd_freq: Optional[int] = None,
        rx_bandwidth: Optional[int] = None,
        tx_bandwidth: Optional[int] = None,
        rx_gain_lna: Optional[int] = None,
        rx_gain_vga: Optional[int] = None,
        rx_gain_pga: Optional[int] = None,
        tx_gain: Optional[int] = None,
        rx_path: Optional[str] = None,
        tx_path: Optional[str] = None,
    ) -> None:
        """Apply multiple RF parameters from one call.

        This mirrors the `dev_data`/CLI controls used by `usdr_dm_create.c` and writes
        corresponding `/dm/sdr/0/*` endpoints. Only non-``None`` values are applied.
        """
        if rx_freq is not None:
            self.set_rx_frequency(rx_freq)
        if tx_freq is not None:
            self.set_tx_frequency(tx_freq)
        if tdd_freq is not None:
            self.set_tdd_frequency(tdd_freq)
        if rx_bandwidth is not None:
            self.set_rx_bandwidth(rx_bandwidth)
        if tx_bandwidth is not None:
            self.set_tx_bandwidth(tx_bandwidth)
        if rx_gain_lna is not None:
            self.set_rx_gain_lna(rx_gain_lna)
        if rx_gain_vga is not None:
            self.set_rx_gain_vga(rx_gain_vga)
        if rx_gain_pga is not None:
            self.set_rx_gain_pga(rx_gain_pga)
        if tx_gain is not None:
            self.set_tx_gain(tx_gain)
        if rx_path is not None:
            self.set_rx_path(rx_path)
        if tx_path is not None:
            self.set_tx_path(tx_path)

    def create_stream(
        self,
        sobj: str,
        dformat: str = "cf32",
        channels: ChannelMap = None,
        pktsyms: int = 4096,
        flags: int = 0,
        parameters: Optional[str] = None,
    ) -> "UsdrStream":
        """Create and return a high-level :class:`UsdrStream` wrapper.

        Args:
            channels: Either list of int channel IDs (``phys_nums``), list of str
                physical channel names (``phys_names``), or ``None`` (defaults to ``[0]``).
        
        Raises:
            UsdrException: When channel type is invalid or stream creation fails.
        """
        self._ensure_open()
        vres, _ = self._lib._validate_channels(channels)
        self._raise_if_error("create_stream(channels)", vres)
        res, handle = self._lib.usdr_dms_create_ex2(self._dev, sobj, dformat, channels, pktsyms, flags, parameters)
        self._raise_if_error("usdr_dms_create_ex2", res)
        return UsdrStream(self, handle)


class UsdrStream:
    """High-level stream wrapper bound to a UsdrDevice."""

    def __init__(self, device: UsdrDevice, handle: ctypes.c_void_p) -> None:
        self._device = device
        self._lib = device._lib
        self._stream = handle
        self._closed = False

    def __enter__(self) -> "UsdrStream":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    @property
    def handle(self) -> ctypes.c_void_p:
        return self._stream

    @staticmethod
    def _raise_if_error(fn_name: str, code: int) -> None:
        if code != 0:
            raise UsdrException(fn_name, code)

    def _ensure_open(self) -> None:
        if self._closed:
            raise UsdrException("stream_closed", -errno.EBADF)

    def close(self) -> None:
        """Close stream and release underlying low-level handle."""
        if not self._closed:
            res = self._lib.usdr_dms_destroy(self._stream)
            self._raise_if_error("usdr_dms_destroy", res)
            self._closed = True

    def info(self) -> UsdrStreamInfo:
        """Return stream metadata as a pure-Python :class:`UsdrStreamInfo` object."""
        self._ensure_open()
        res, info = self._lib.usdr_dms_info(self._stream)
        self._raise_if_error("usdr_dms_info", res)
        return UsdrStreamInfo.from_ctypes(info)

    def op(self, command: int, tm: int = 0) -> None:
        """Run stream operation command (start/stop/timed variants)."""
        self._ensure_open()
        res = self._lib.usdr_dms_op(self._stream, command, tm)
        self._raise_if_error("usdr_dms_op", res)

    def sync(self, synctype: str = "off", *other_streams: "UsdrStream") -> None:
        """Synchronize this stream together with optional peer streams."""
        self._ensure_open()
        streams = [self._stream]
        for st in other_streams:
            st._ensure_open()
            streams.append(st._stream)
        res = self._lib.usdr_dms_sync(self._device.handle, synctype, streams)
        self._raise_if_error("usdr_dms_sync", res)

    def recv(self, timeout_ms: int, with_info: bool = True):
        """Receive one block as numpy arrays; returns ``(arrays, recv_info)``."""
        self._ensure_open()
        res, arrays, rx_nfo = self._lib.usdr_dms_recv_numpy(self._stream, timeout_ms=timeout_ms, with_info=with_info)
        self._raise_if_error("usdr_dms_recv", res)
        return arrays, rx_nfo

    def send(self, buffers, timestamp: int = 0, timeout_ms: int = 1000, samples: Optional[int] = None) -> None:
        """Send one block from numpy arrays using stored stream format metadata."""
        self._ensure_open()
        res = self._lib.usdr_dms_send_numpy(self._stream, buffers, timestamp=timestamp, timeout_ms=timeout_ms, samples=samples)
        self._raise_if_error("usdr_dms_send", res)
