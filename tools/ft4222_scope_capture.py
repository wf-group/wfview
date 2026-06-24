#!/usr/bin/env python3
"""
Capture raw Yaesu FT4222 scope data for wfview analysis.

This script talks directly to the FTDI D2XX/FT4222 shared libraries with
ctypes. It does not require a Python FT4222 package.

Linux requirements:
  - Python 3
  - FTDI D2XX and LibFT4222 installed and visible to the dynamic linker
    (for example libft4222.so and libftd2xx.so in /usr/local/lib)
  - Permission to open the FT4222 device. If udev rules are not installed,
    run with sudo.

Typical use:
  sudo python3 tools/ft4222_scope_capture.py --frames 50

Useful options:
  --init                            Send ASCII INIT before reading
  --out ft710_marscap_capture.bin   Raw concatenated SPI reads
  --summary ft710_marscap_summary.csv
  --read-size 4096                  wfview's expected frame size
  --read-size 8192                  Try this if the normal capture never syncs

Please send both the .bin and .csv files back for analysis.
"""

from __future__ import annotations

import argparse
import ctypes
import csv
import datetime as _dt
import os
import platform
import sys
import time
from pathlib import Path


FT_OK = 0
FT4222_OK = 0

FT_OPEN_BY_DESCRIPTION = 2

SPI_IO_SINGLE = 1
CLK_DIV_64 = 6
CLK_IDLE_HIGH = 1
CLK_LEADING = 0
SYS_CLK_24 = 1

SYNC = b"\xff\x01\xee\x01"


class Ft4222CaptureError(RuntimeError):
    pass


def _candidate_paths(names: list[str]) -> list[str]:
    roots = [
        Path.cwd(),
        Path.cwd() / "wfview-build" / "wfview-release",
        Path.cwd() / "wfserver-build" / "wfserver-release",
        Path.cwd().parent / "LibFT4222-v1.4.7" / "imports" / "LibFT4222" / "dll" / "amd64",
        Path.cwd().parent / "LibFT4222-v1.4.7" / "imports" / "ftd2xx" / "dll" / "amd64",
    ]
    paths = names[:]
    for root in roots:
        for name in names:
            paths.append(str(root / name))
    return paths


def _dll_loader():
    return ctypes.WinDLL if platform.system() == "Windows" else ctypes.CDLL


def _load_library(names: list[str]):
    last_error: Exception | None = None
    for name in _candidate_paths(names):
        try:
            return _dll_loader()(name)
        except OSError as exc:
            last_error = exc
    raise Ft4222CaptureError(
        "Could not load any of: "
        + ", ".join(names)
        + (f"\nLast loader error: {last_error}" if last_error else "")
    )


def load_libraries() -> tuple[ctypes.CDLL, ctypes.CDLL]:
    ft4222 = _load_library(
        [
            "LibFT4222-64.dll",
            "LibFT4222.dll",
            "libft4222.so",
            "libft4222.so.1.4.4.44",
            "/usr/local/lib/libft4222.so",
            "/usr/local/lib/libft4222.so.1.4.4.44",
        ]
    )

    # Some Linux LibFT4222 packages also export the D2XX entry points.
    if hasattr(ft4222, "FT_OpenEx"):
        return ft4222, ft4222

    ftd2xx = _load_library(
        [
            "ftd2xx.dll",
            "ftd2xx64.dll",
            "libftd2xx.so",
            "/usr/local/lib/libftd2xx.so",
            "/usr/lib/libftd2xx.so",
            "/usr/lib/x86_64-linux-gnu/libftd2xx.so",
        ]
    )
    return ft4222, ftd2xx


def configure_functions(ft4222: ctypes.CDLL, ftd2xx: ctypes.CDLL) -> None:
    ftd2xx.FT_OpenEx.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_void_p)]
    ftd2xx.FT_OpenEx.restype = ctypes.c_ulong
    ftd2xx.FT_Close.argtypes = [ctypes.c_void_p]
    ftd2xx.FT_Close.restype = ctypes.c_ulong
    ftd2xx.FT_SetTimeouts.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong]
    ftd2xx.FT_SetTimeouts.restype = ctypes.c_ulong
    ftd2xx.FT_SetLatencyTimer.argtypes = [ctypes.c_void_p, ctypes.c_ubyte]
    ftd2xx.FT_SetLatencyTimer.restype = ctypes.c_ulong

    ft4222.FT4222_UnInitialize.argtypes = [ctypes.c_void_p]
    ft4222.FT4222_UnInitialize.restype = ctypes.c_ulong
    ft4222.FT4222_SPIMaster_Init.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_ubyte,
    ]
    ft4222.FT4222_SPIMaster_Init.restype = ctypes.c_ulong
    ft4222.FT4222_SPIMaster_SingleRead.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_ubyte),
        ctypes.c_ushort,
        ctypes.POINTER(ctypes.c_ushort),
        ctypes.c_bool,
    ]
    ft4222.FT4222_SPIMaster_SingleRead.restype = ctypes.c_ulong
    if hasattr(ft4222, "FT4222_SPIMaster_SingleWrite"):
        ft4222.FT4222_SPIMaster_SingleWrite.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_ushort,
            ctypes.POINTER(ctypes.c_ushort),
            ctypes.c_bool,
        ]
        ft4222.FT4222_SPIMaster_SingleWrite.restype = ctypes.c_ulong
    ft4222.FT4222_SetClock.argtypes = [ctypes.c_void_p, ctypes.c_int]
    ft4222.FT4222_SetClock.restype = ctypes.c_ulong


def check_status(name: str, status: int, ok: int = FT_OK) -> None:
    if status != ok:
        raise Ft4222CaptureError(f"{name} failed with status {status}")


def open_device(ft4222: ctypes.CDLL, ftd2xx: ctypes.CDLL, description: str) -> ctypes.c_void_p:
    handle = ctypes.c_void_p()
    desc = ctypes.create_string_buffer(description.encode("ascii"))
    check_status("FT_OpenEx", ftd2xx.FT_OpenEx(desc, FT_OPEN_BY_DESCRIPTION, ctypes.byref(handle)))
    try:
        check_status("FT_SetTimeouts", ftd2xx.FT_SetTimeouts(handle, 100, 100))
        check_status("FT_SetLatencyTimer", ftd2xx.FT_SetLatencyTimer(handle, 2))
        check_status(
            "FT4222_SPIMaster_Init",
            ft4222.FT4222_SPIMaster_Init(
                handle,
                SPI_IO_SINGLE,
                CLK_DIV_64,
                CLK_IDLE_HIGH,
                CLK_LEADING,
                0x01,
            ),
            FT4222_OK,
        )
        check_status("FT4222_SetClock", ft4222.FT4222_SetClock(handle, SYS_CLK_24), FT4222_OK)
    except Exception:
        try:
            ft4222.FT4222_UnInitialize(handle)
        finally:
            ftd2xx.FT_Close(handle)
        raise
    return handle


def hexdump_prefix(data: bytes, length: int = 16) -> str:
    return data[:length].hex(" ")


def write_spi(ft4222: ctypes.CDLL, handle: ctypes.c_void_p, payload: bytes) -> int:
    if not payload:
        return 0
    if not hasattr(ft4222, "FT4222_SPIMaster_SingleWrite"):
        raise Ft4222CaptureError("FT4222_SPIMaster_SingleWrite is not available in the loaded library")

    actual = ctypes.c_ushort()
    buffer = (ctypes.c_ubyte * len(payload)).from_buffer_copy(payload)
    check_status(
        "FT4222_SPIMaster_SingleWrite",
        ft4222.FT4222_SPIMaster_SingleWrite(
            handle,
            buffer,
            len(payload),
            ctypes.byref(actual),
            True,
        ),
        FT4222_OK,
    )
    return actual.value


def capture(args: argparse.Namespace) -> int:
    if args.read_size < 1 or args.read_size > 65535:
        raise Ft4222CaptureError("--read-size must be between 1 and 65535")

    ft4222, ftd2xx = load_libraries()
    configure_functions(ft4222, ftd2xx)

    out_path = Path(args.out)
    summary_path = Path(args.summary)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"Opening FT4222 device by description: {args.description!r}")
    print(f"Raw output: {out_path}")
    print(f"Summary:    {summary_path}")
    print(f"Read size:  {args.read_size} bytes")

    handle = open_device(ft4222, ftd2xx, args.description)
    if args.init:
        written = write_spi(ft4222, handle, b"INIT")
        print(f"Wrote SPI init: INIT ({written} bytes)")

    buffer_type = ctypes.c_ubyte * args.read_size
    buffer = buffer_type()
    actual = ctypes.c_ushort()

    deadline = time.monotonic() + args.seconds if args.seconds is not None else None
    frame_count = 0
    sync_ok = 0
    sync_bad = 0
    short_reads = 0
    started = _dt.datetime.now(_dt.timezone.utc).isoformat()

    try:
        with out_path.open("wb") as raw, summary_path.open("w", newline="", encoding="utf-8") as summary:
            writer = csv.writer(summary)
            writer.writerow(
                [
                    "frame",
                    "utc_iso",
                    "elapsed_ms",
                    "status",
                    "bytes",
                    "ends_with_ff01ee01",
                    "sync_offsets",
                    "first_16_hex",
                    "last_16_hex",
                ]
            )
            start = time.monotonic()
            while frame_count < args.frames and (deadline is None or time.monotonic() < deadline):
                status = ft4222.FT4222_SPIMaster_SingleRead(
                    handle,
                    buffer,
                    args.read_size,
                    ctypes.byref(actual),
                    False,
                )
                now = time.monotonic()
                data = bytes(buffer[: actual.value])
                raw.write(data)

                ends_sync = data.endswith(SYNC)
                offsets = []
                pos = data.find(SYNC)
                while pos != -1:
                    offsets.append(str(pos))
                    pos = data.find(SYNC, pos + 1)

                if ends_sync:
                    sync_ok += 1
                else:
                    sync_bad += 1
                if actual.value != args.read_size:
                    short_reads += 1

                writer.writerow(
                    [
                        frame_count,
                        _dt.datetime.now(_dt.timezone.utc).isoformat(),
                        int((now - start) * 1000),
                        status,
                        actual.value,
                        int(ends_sync),
                        " ".join(offsets),
                        hexdump_prefix(data),
                        data[-16:].hex(" ") if data else "",
                    ]
                )
                frame_count += 1
                if args.verbose:
                    print(
                        f"frame={frame_count:05d} status={status} bytes={actual.value} "
                        f"end_sync={ends_sync} sync_offsets={','.join(offsets) or '-'}"
                    )

    finally:
        try:
            ft4222.FT4222_UnInitialize(handle)
        finally:
            ftd2xx.FT_Close(handle)

    print()
    print("Capture complete")
    print(f"Started UTC:         {started}")
    print(f"Frames captured:     {frame_count}")
    print(f"Frames ending sync:  {sync_ok}")
    print(f"Frames missing sync: {sync_bad}")
    print(f"Short reads:         {short_reads}")
    print()
    print("Please send these files for analysis:")
    print(f"  {out_path}")
    print(f"  {summary_path}")
    return 0 if frame_count > 0 else 1


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture raw FT4222 SPI scope data from Yaesu radios for wfview analysis."
    )
    timestamp = _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    parser.add_argument("--frames", type=int, default=50, help="number of raw SPI reads to capture")
    parser.add_argument("--seconds", type=float, default=None, help="optional maximum capture duration")
    parser.add_argument("--read-size", type=int, default=4096, help="bytes per SPI read")
    parser.add_argument("--description", default="FT4222 A", help="FTDI device description")
    parser.add_argument("--init", action="store_true", help="send ASCII INIT over SPI before reading")
    parser.add_argument("--out", default=f"ft4222_scope_{timestamp}.bin", help="raw binary output file")
    parser.add_argument(
        "--summary",
        default=f"ft4222_scope_{timestamp}.csv",
        help="CSV summary output file",
    )
    parser.add_argument("--verbose", action="store_true", help="print one line per read")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        return capture(args)
    except KeyboardInterrupt:
        print("\nInterrupted", file=sys.stderr)
        return 130
    except Ft4222CaptureError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        print(
            "\nCheck that libft4222/libftd2xx are installed, wfview is not running, "
            "and you have permission to access the FT4222 device.",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
