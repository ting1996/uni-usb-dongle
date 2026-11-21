#!/usr/bin/env python3
"""Capture M62 UART logs with CH340 DTR/RTS deasserted.

The Linux CH340 driver can still pulse the lines during open, briefly resetting
the board. Start this monitor before connecting HID/audio clients, then keep it
open throughout the test instead of reopening it between measurements.
"""
import argparse
from contextlib import nullcontext
import sys
import time


def open_uart(serial_module, device, baudrate):
    port = serial_module.Serial(port=None, baudrate=baudrate, timeout=0.25)
    # Configure BEFORE open to avoid leaving default True values applied.
    # A driver-level opening pulse can still reset this board once.
    port.dtr = False
    port.rts = False
    port.port = device
    port.open()
    return port


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--output", help="append raw UART bytes to this log")
    args = parser.parse_args()
    if args.baud <= 0 or args.duration <= 0:
        parser.error("baud and duration must be positive")
    deadline = time.monotonic() + args.duration
    output = open(args.output, "ab") if args.output else nullcontext()
    with open_uart(serial, args.port, args.baud) as port, output as log:
        while time.monotonic() < deadline:
            data = port.read(4096)
            if data:
                if log:
                    log.write(data)
                    log.flush()
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()


if __name__ == "__main__":
    main()
