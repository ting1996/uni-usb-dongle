#!/usr/bin/env python3
"""Inspect the dongle radio over HID; pairing requires an explicit address."""
import argparse
import json
import os
import struct
import time
if __package__:
    from .usb_hid_check import HidPort
    from .usb_device_check import _request, read_device_state
else:
    from usb_hid_check import HidPort
    from usb_device_check import _request, read_device_state


def address(value):
    parts = value.split(":")
    if len(parts) != 6 or any(len(p) != 2 for p in parts):
        raise argparse.ArgumentTypeError("address must be AA:BB:CC:DD:EE:FF")
    try:
        return bytes(int(p, 16) for p in parts)
    except ValueError as error:
        raise argparse.ArgumentTypeError("invalid Bluetooth address") from error


def format_address(value):
    return ":".join(f"{byte:02X}" for byte in value)


def radio_state(port):
    data = _request(port, 0x18, 100, b"", 3)
    if len(data) not in (33, 44) or data[0]:
        raise ValueError(f"invalid radio status: {data.hex()}")
    state = dict(ready=bool(data[1]), scanning=bool(data[2]), count=data[3],
                error=data[4], controllers=data[5], streaming=bool(data[6]),
                headphone=format_address(data[7:13]),
                **dict(zip(("usb_bytes", "bluetooth_bytes", "underflows", "overflows", "audio_errors"),
                           struct.unpack_from("<5I", data, 13))))
    if len(data) == 44:
        state.update(audio_state=data[33], audio_error_stage=data[34], audio_error=data[35],
                     audio_target=format_address(data[36:42]),
                     audio_connected=bool(data[42]), audio_authenticated=bool(data[43]))
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="auto" if os.name == "nt" else "/dev/hidraw0")
    parser.add_argument("--scan", action="store_true")
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--pair-controller", type=address)
    actions.add_argument("--pair-audio", type=address)
    actions.add_argument("--disconnect", type=address)
    parser.add_argument("--wait", type=float, default=0, help="seconds to observe connection progress")
    args = parser.parse_args()
    with HidPort(args.device) as port:
        if args.scan:
            data = _request(port, 0x13, 101, b"", 3)
            if data != b"\0": raise ValueError(f"discovery rejected: {data.hex()}")
            deadline = time.monotonic() + 25
            while radio_state(port)["scanning"]:
                if time.monotonic() > deadline: raise TimeoutError("discovery did not finish")
                time.sleep(0.5)
            for i in range(radio_state(port)["count"]):
                data = _request(port, 0x17, 102+i, bytes([i]), 3)
                if len(data) != 39 or data[0]: raise ValueError("invalid discovery entry")
                print(json.dumps(dict(address=format_address(data[2:8]),
                    rssi=struct.unpack("b",data[8:9])[0],
                    name=data[9:].split(b"\0")[0].decode("utf-8",errors="replace"))), flush=True)
        for command, value in ((0x14,args.pair_controller),(0x19,args.pair_audio),(0x1a,args.disconnect)):
            if value:
                data = _request(port, command, 120, value, 3)
                if data != b"\0": raise ValueError(f"connection request rejected: {data.hex()}")
        deadline = time.monotonic() + args.wait
        while True:
            print(json.dumps(radio_state(port)), flush=True)
            if time.monotonic() >= deadline: break
            time.sleep(min(2, deadline-time.monotonic()))
        print(read_device_state(port, sequence=200), flush=True)


if __name__ == "__main__":
    main()
