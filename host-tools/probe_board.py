#!/usr/bin/env python3
"""
probe_board.py — minimal Shrike-Fi USB probe / bring-up helper.

What it does:
  * lists serial ports and flags the Shrike-Fi's ESP32-S3 USB Serial/JTAG
    (VID 0x303A, PID 0x1001) and any CDC/UART ports,
  * optionally opens a port, reads whatever the firmware is printing, and runs
    a couple of console commands ("info", "fpga_load") so you can sanity-check
    the `firmware/bringup` app without firing up a full terminal.

This is intentionally tiny and dependency-light (just pyserial). It is NOT the
wallet client — that lands in host-tools/shrikevault_cli.py (P4).

Usage:
    python probe_board.py                 # just list ports
    python probe_board.py --port COM19    # open, read banner, run a few commands
    python probe_board.py --port COM19 --cmd info --cmd fpga_load --cmd io_read
"""
from __future__ import annotations

import argparse
import sys
import time

try:
    import serial  # pyserial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")

ESP32S3_USB_SERIAL_JTAG = (0x303A, 0x1001)  # Espressif built-in USB Serial/JTAG


def list_serial_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("(no serial ports found)")
        return
    print(f"{'PORT':<12} {'VID:PID':<12} DESCRIPTION")
    for p in ports:
        vidpid = f"{p.vid:04X}:{p.pid:04X}" if (p.vid and p.pid) else "-"
        tag = ""
        if p.vid and p.pid and (p.vid, p.pid) == ESP32S3_USB_SERIAL_JTAG:
            tag = "  <-- ESP32-S3 USB Serial/JTAG (Shrike-Fi)"
        print(f"{p.device:<12} {vidpid:<12} {p.description}{tag}")


def talk(port: str, cmds: list[str], read_secs: float) -> None:
    print(f"opening {port} ...")
    # Baud is irrelevant for the ESP32-S3 native USB CDC, but pyserial wants one.
    with serial.Serial(port, 115200, timeout=0.3) as s:
        time.sleep(0.2)
        # nudge the REPL to print its prompt
        s.write(b"\r\n")
        end = time.time() + read_secs
        buf = b""
        while time.time() < end:
            chunk = s.read(4096)
            if chunk:
                buf += chunk
        if buf:
            print("--- output ---")
            print(buf.decode("utf-8", "replace"))
        for c in cmds:
            print(f"--- > {c} ---")
            s.write(c.encode() + b"\r\n")
            time.sleep(0.6)
            out = s.read(8192)
            print(out.decode("utf-8", "replace"))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port to open (e.g. COM19); omit to just list ports")
    ap.add_argument("--cmd", action="append", default=[], metavar="CMD",
                    help="console command to send after connecting (repeatable). "
                         "Default if --port given and no --cmd: 'info'")
    ap.add_argument("--read-secs", type=float, default=1.5, help="seconds to read the boot banner first")
    args = ap.parse_args()

    print("== serial ports ==")
    list_serial_ports()
    if not args.port:
        print("\n(tip: pass --port COM19 to talk to the bringup firmware)")
        return
    cmds = args.cmd or ["info"]
    print()
    talk(args.port, cmds, args.read_secs)


if __name__ == "__main__":
    main()
