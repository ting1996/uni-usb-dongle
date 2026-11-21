#!/usr/bin/env python3
"""Verify both real USB HID interfaces using explicit, transient test reports.

Requires access to Linux joystick nodes and management hidraw (audio firmware)
or pyserial (legacy CDC firmware).
Kernel HID drivers stay attached and no USB permission changes are needed.
Reports return to neutral after 500 ms. Does not modify pairing records or flash.
Run only when gamepad input will not interfere with a foreground application.
"""

import argparse
from contextlib import ExitStack
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import re
import select
import struct
import sys
import time

if __package__:
    from . import usb_device_check, usb_ota
else:
    import usb_device_check
    import usb_ota

CMD_TEST_GAMEPAD = 0x15
CMD_GET_GAMEPAD_DIAGNOSTICS = 0x16
NEUTRAL = bytes((128, 128, 128, 128, 0, 0, 0, 0, 8))
REPORTS = (
    bytes((32, 220, 90, 160, 64, 128, 1, 0, 2)),
    bytes((200, 50, 180, 70, 160, 32, 0, 128, 6)),
)


def usb_device_path(port, sysfs=Path("/sys/class/tty")):
    path = (sysfs / Path(port).resolve().name / "device").resolve()
    for parent in (path, *path.parents):
        if (parent / "idVendor").is_file() and (parent / "busnum").is_file():
            return parent
    raise ValueError(f"cannot resolve USB identity for {port}")


def request_report(port, slot, report, sequence, timeout):
    response = usb_device_check._request(
        port, CMD_TEST_GAMEPAD, sequence, bytes((slot,)) + report, timeout
    )
    if response != bytes((usb_device_check.STATUS_OK, 0, 0, 0, 0)):
        raise ValueError(f"test report rejected: {response.hex()}")


def read_diagnostics(port, sequence, timeout):
    response = usb_device_check._request(
        port, CMD_GET_GAMEPAD_DIAGNOSTICS, sequence, b"\x00", timeout
    )
    if len(response) != 27 or response[0] != usb_device_check.STATUS_OK:
        raise ValueError("invalid gamepad diagnostics response")
    return struct.unpack("<4I", response[11:])


def check_tick_rate(port, timeout):
    first = read_diagnostics(port, 0x10000, timeout)
    start = time.monotonic()
    time.sleep(0.25)
    second = read_diagnostics(port, 0x10001, timeout)
    elapsed = time.monotonic() - start
    ticks = (second[2] - first[2]) & 0xffffffff
    rate = ticks / elapsed
    if not 700 <= rate <= 1300:
        raise ValueError(f"RTOS clock is {rate:.1f} ticks/s; expected 1000")
    print(f"RTOS clock: {rate:.1f} ticks/s (expected 1000)")


def joystick_expected(report, corrections):
    # Linux orders axes by ABS code: X,Y,Z,Rx,Ry,Rz,HAT0X,HAT0Y.
    axes = [report[i] for i in (0, 1, 4, 2, 3, 5)]
    hats = ((0, -1), (1, -1), (1, 0), (1, 1), (0, 1),
            (-1, 1), (-1, 0), (-1, -1), (0, 0))
    axes.extend(hats[report[8]])
    # Apply the read-only JSIOCGCORR calibration, including its dead zone.
    # Linux ABI: https://github.com/torvalds/linux/blob/master/drivers/input/joydev.c
    for i, correction in enumerate(corrections):
        if correction[9] == 1:
            lower, upper, negative_scale, positive_scale = correction[:4]
            value = axes[i]
            if value <= lower:
                value = (value - lower) * negative_scale // 16384
            elif value >= upper:
                value = (value - upper) * positive_scale // 16384
            else:
                value = 0
            axes[i] = max(-32767, min(32767, value))
        elif correction[9] != 0:
            raise ValueError("unsupported Linux joystick calibration type")
    buttons = [(int.from_bytes(report[6:8], "little") >> i) & 1
               for i in range(16)]
    return axes, buttons


def _match_windows_gamepads(devices):
    by_intf = {}
    for item in devices:
        if (item["usage_page"] != 0x0001 or item["usage"] != 0x0005 or
                item["input_len"] != 10 or
                item["input_button_caps"] != 1 or
                item["input_value_caps"] != 7):
            continue
        match = re.search(r"&mi_([0-9a-f]+)", item["path"].lower())
        if not match:
            continue
        intf_number = int(match.group(1), 16)
        by_intf[intf_number] = item["path"]
    if 4 in by_intf and 5 in by_intf:
        return {0: by_intf[4], 1: by_intf[5]}
    if 4 in by_intf and 3 not in by_intf:
        return {0: by_intf[4]}
    if 3 in by_intf and 4 in by_intf:
        return {0: by_intf[3], 1: by_intf[4]}
    if 2 in by_intf and 3 in by_intf:
        return {0: by_intf[2], 1: by_intf[3]}
    raise ValueError(
        "expected Windows HID game controller interfaces on MI_04[/MI_05], MI_03/MI_04, or legacy MI_02 and MI_03"
    )


def find_windows_gamepads(vid=None, pid=None):
    """Discover both Windows HID gamepad device paths mapped to slots 0 and 1."""
    if os.name != "nt":
        return {}
    if __package__:
        from . import usb_hid_check
    else:
        import usb_hid_check

    return _match_windows_gamepads(usb_hid_check.find_windows_hid_devices(vid, pid))


def check_gamepads_windows(port_name, timeout=3.0, cycles=1, hid=True):
    if __package__:
        from . import usb_hid_check
    else:
        import usb_hid_check

    if hid:
        port_context = lambda: usb_hid_check.HidPort(port_name)
    else:
        import serial
        port_context = lambda: serial.Serial(
            port_name, baudrate=115200, timeout=0.1, write_timeout=5
        )

    nodes = find_windows_gamepads()
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    hid_dll = ctypes.WinDLL("hid", use_last_error=True)

    INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    FILE_SHARE_READ = 0x01
    FILE_SHARE_WRITE = 0x02
    OPEN_EXISTING = 3
    FILE_FLAG_OVERLAPPED = 0x40000000
    ERROR_IO_PENDING = 997
    WAIT_FAILED = 0xFFFFFFFF
    WAIT_TIMEOUT = 0x00000102

    class OVERLAPPED(ctypes.Structure):
        _fields_ = [
            ("Internal", ctypes.c_size_t),
            ("InternalHigh", ctypes.c_size_t),
            ("Offset", wintypes.DWORD),
            ("OffsetHigh", wintypes.DWORD),
            ("hEvent", wintypes.HANDLE),
        ]

    kernel32.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOLEAN
    kernel32.CreateEventW.argtypes = [
        ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR
    ]
    kernel32.CreateEventW.restype = wintypes.HANDLE
    kernel32.ResetEvent.argtypes = [wintypes.HANDLE]
    kernel32.ResetEvent.restype = wintypes.BOOLEAN
    kernel32.ReadFile.argtypes = [
        wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(OVERLAPPED),
    ]
    kernel32.ReadFile.restype = wintypes.BOOL
    kernel32.GetOverlappedResult.argtypes = [
        wintypes.HANDLE, ctypes.POINTER(OVERLAPPED),
        ctypes.POINTER(wintypes.DWORD), wintypes.BOOL,
    ]
    kernel32.GetOverlappedResult.restype = wintypes.BOOL
    kernel32.WaitForMultipleObjects.argtypes = [
        wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE), wintypes.BOOL, wintypes.DWORD
    ]
    kernel32.WaitForMultipleObjects.restype = wintypes.DWORD
    kernel32.CancelIo.argtypes = [wintypes.HANDLE]
    kernel32.CancelIo.restype = wintypes.BOOL
    hid_dll.HidD_GetInputReport.argtypes = [
        wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG
    ]
    hid_dll.HidD_GetInputReport.restype = wintypes.BOOLEAN

    with ExitStack() as cleanup:
        slot_count = len(nodes)
        handles = {}
        events = (wintypes.HANDLE * slot_count)()
        buffers = {}
        overlappeds = {}
        pending_io = {}
        states = [None] * slot_count

        def arm_read(slot):
            while True:
                kernel32.ResetEvent(events[slot])
                ov = overlappeds[slot]
                ov.Internal = 0
                ov.InternalHigh = 0
                ov.Offset = 0
                ov.OffsetHigh = 0
                bytes_read = wintypes.DWORD(0)
                ok = kernel32.ReadFile(
                    handles[slot], buffers[slot], 10,
                    ctypes.byref(bytes_read), ctypes.byref(ov)
                )
                if ok:
                    data = bytes(buffers[slot])
                    if bytes_read.value != 10 or data[0] != 1:
                        raise ValueError(f"invalid Windows HID input report on slot {slot}")
                    states[slot] = data[1:10]
                    continue
                err = ctypes.get_last_error()
                if err == ERROR_IO_PENDING:
                    pending_io[slot] = True
                    return
                raise ctypes.WinError(err)

        for slot in range(slot_count):
            event = kernel32.CreateEventW(None, True, False, None)
            if not event:
                raise ctypes.WinError(ctypes.get_last_error())
            cleanup.callback(kernel32.CloseHandle, event)
            handle = kernel32.CreateFileW(
                nodes[slot],
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                None,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                None,
            )
            if handle == INVALID_HANDLE_VALUE or handle is None:
                raise ctypes.WinError(ctypes.get_last_error())
            cleanup.callback(kernel32.CloseHandle, handle)
            cleanup.callback(kernel32.CancelIo, handle)

            handles[slot] = handle
            events[slot] = event
            buffers[slot] = (ctypes.c_ubyte * 10)()
            overlappeds[slot] = OVERLAPPED(hEvent=event)
            pending_io[slot] = False
            arm_read(slot)
            print(f"Gamepad {slot}: {nodes[slot]}, 6 axes + hat, 16 buttons")

        def wait_for(reports):
            expected = [bytes(report) for report in reports]
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if all(states[slot] == expected[slot] for slot in range(slot_count)):
                    return
                remaining_ms = max(1, min(50, int((deadline - time.monotonic()) * 1000)))
                wait_rc = kernel32.WaitForMultipleObjects(slot_count, events, False, remaining_ms)
                if wait_rc == WAIT_FAILED:
                    raise ctypes.WinError(ctypes.get_last_error())
                if wait_rc == WAIT_TIMEOUT:
                    continue
                if wait_rc < slot_count:
                    slot = wait_rc
                    transferred = wintypes.DWORD(0)
                    if not kernel32.GetOverlappedResult(
                        handles[slot], ctypes.byref(overlappeds[slot]),
                        ctypes.byref(transferred), False
                    ):
                        raise ctypes.WinError(ctypes.get_last_error())
                    data = bytes(buffers[slot])
                    if transferred.value != 10 or data[0] != 1:
                        raise ValueError(f"invalid Windows HID input report on slot {slot}")
                    states[slot] = data[1:10]
                    pending_io[slot] = False
                    arm_read(slot)
            raise TimeoutError(
                f"joystick state mismatch: got {[s.hex() if s else None for s in states]}; "
                f"expected {[e.hex() for e in expected]}"
            )

        with port_context() as port:
            if not hid:
                port.dtr = False
                port.rts = False
            state = usb_device_check.read_device_state(port, timeout=timeout)
            if not state.capabilities & usb_device_check.CAPABILITY_USB_GAMEPAD:
                raise ValueError("USB gamepad capability is not advertised")
            check_tick_rate(port, timeout)
            sequence = 1
            for payload in (b"", bytes((2,)) + NEUTRAL,
                            bytes((0,)) + NEUTRAL[:-1] + b"\x09"):
                response = usb_device_check._request(
                    port, CMD_TEST_GAMEPAD, sequence, payload, timeout
                )
                sequence += 1
                if response != bytes((1, 0, 0, 0, 0)):
                    raise ValueError("malformed gamepad diagnostic was not rejected")
            print("Malformed diagnostic commands rejected")
            for slot in range(slot_count):
                request_report(port, slot, NEUTRAL, sequence, timeout)
                sequence += 1
            wait_for([NEUTRAL] * slot_count)
            for slot in range(slot_count):
                get_buf = (ctypes.c_ubyte * 10)()
                get_buf[0] = 1
                if not hid_dll.HidD_GetInputReport(handles[slot], get_buf, 10):
                    raise ctypes.WinError(ctypes.get_last_error())
                if bytes(get_buf[1:10]) != NEUTRAL:
                    raise ValueError(f"HidD_GetInputReport mismatch on slot {slot}")
            try:
                for cycle in range(cycles):
                    for slot, report in enumerate(REPORTS[:slot_count]):
                        request_report(port, slot, report, sequence, timeout)
                        sequence += 1
                        expected = [NEUTRAL] * slot_count
                        expected[slot] = report
                        wait_for(expected)
                        wait_for([NEUTRAL] * slot_count)
                    print(f"Cycle {cycle + 1}/{cycles}: axes/buttons/hat, "
                          "slot isolation, and automatic release passed")
                for slot, report in enumerate(REPORTS[:slot_count]):
                    request_report(port, slot, report, sequence, timeout)
                    sequence += 1
                wait_for(REPORTS[:slot_count])
                wait_for([NEUTRAL] * slot_count)
                print("Simultaneous input on both gamepads and automatic release passed")
            finally:
                for slot in range(slot_count):
                    try:
                        request_report(port, slot, NEUTRAL, sequence, timeout)
                    except (OSError, RuntimeError, TimeoutError, ValueError):
                        pass
                    sequence += 1
            final_state = usb_device_check.read_device_state(
                port, sequence=sequence, timeout=timeout
            )
            if final_state != state:
                raise ValueError("device catalog changed during HID diagnostics")
            usb_ota.probe(port, retries=0, timeout=timeout)
            print(f"{'HID' if hid else 'CDC'} status/OTA probe still works; saved controller state unchanged")


def check_gamepads(port_name, timeout=3.0, cycles=1, hid=False):
    if os.name == "nt":
        return check_gamepads_windows(port_name, timeout=timeout, cycles=cycles, hid=hid)
    import fcntl
    if hid:
        if __package__:
            from .usb_hid_check import HidPort
        else:
            from usb_hid_check import HidPort
        port_context = lambda: HidPort(port_name)
    else:
        import serial
        port_context = lambda: serial.Serial(port_name, baudrate=115200, timeout=0.1, write_timeout=5)

    usb_path = usb_device_path(port_name, Path("/sys/class/hidraw") if hid else Path("/sys/class/tty"))
    by_intf = {}
    for path in Path("/sys/class/input").glob("js*"):
        resolved = (path / "device").resolve()
        if usb_path not in resolved.parents:
            continue
        for parent in resolved.parents:
            number_file = parent / "bInterfaceNumber"
            if number_file.is_file():
                number = int(number_file.read_text(), 16)
                by_intf[number] = Path("/dev/input") / path.name
                break
    if 3 in by_intf and 4 in by_intf:
        nodes = {0: by_intf[3], 1: by_intf[4]}
    elif 2 in by_intf and 3 in by_intf:
        nodes = {0: by_intf[2], 1: by_intf[3]}
    else:
        raise ValueError("expected two kernel joystick nodes on interfaces 3/4 or 2/3")

    with ExitStack() as cleanup:
        states = []
        corrections = []
        fd_slots = {}
        for slot in range(2):
            fd = os.open(nodes[slot], os.O_RDONLY | os.O_NONBLOCK)
            cleanup.callback(os.close, fd)
            axes_count = bytearray(1)
            button_count = bytearray(1)
            fcntl.ioctl(fd, 0x80016a11, axes_count, True)  # JSIOCGAXES
            fcntl.ioctl(fd, 0x80016a12, button_count, True)  # JSIOCGBUTTONS
            if axes_count[0] != 8 or button_count[0] != 16:
                raise ValueError("gamepad must expose six axes, hat XY, 16 buttons")
            mapping = bytearray(64)
            fcntl.ioctl(fd, 0x80406a32, mapping, True)  # JSIOCGAXMAP
            if list(mapping[:8]) != [0, 1, 2, 3, 4, 5, 16, 17]:
                raise ValueError("unexpected Linux joystick axis mapping")
            correction_data = bytearray(36 * 64)
            fcntl.ioctl(fd, 0x80246a22, correction_data, True)  # JSIOCGCORR
            corrections.append([struct.unpack_from("<8ihH", correction_data, 36 * i)
                                for i in range(8)])
            states.append(([None] * 8, [None] * 16))
            fd_slots[fd] = slot
            print(f"Gamepad {slot}: {nodes[slot]}, 8 axes, 16 buttons")

        def wait_for(reports):
            expected = [joystick_expected(report, correction)
                        for report, correction in zip(reports, corrections)]
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                matched = True
                for actual, wanted in zip(states, expected):
                    if (actual[1] != wanted[1] or
                            any(a is None or abs(a - b) > 1
                                for a, b in zip(actual[0], wanted[0]))):
                        matched = False
                if matched:
                    return
                readable, _, _ = select.select(list(fd_slots), [], [], 0.05)
                for fd in readable:
                    data = os.read(fd, 4096)
                    if not data or len(data) % 8:
                        raise ValueError("invalid Linux joystick event stream")
                    for _, value, kind, number in struct.iter_unpack("<IhBB", data):
                        kind &= 0x7f  # Include initial-state events.
                        target = states[fd_slots[fd]]
                        if kind == 1 and number < 16:
                            target[1][number] = value
                        elif kind == 2 and number < 8:
                            target[0][number] = value
            raise TimeoutError(f"joystick state mismatch: got {states}; "
                               f"expected {expected}")

        with port_context() as port:
            if not hid:
                port.dtr = False
                port.rts = False
            state = usb_device_check.read_device_state(port, timeout=timeout)
            if not state.capabilities & usb_device_check.CAPABILITY_USB_GAMEPAD:
                raise ValueError("USB gamepad capability is not advertised")
            check_tick_rate(port, timeout)
            sequence = 1
            for payload in (b"", bytes((2,)) + NEUTRAL,
                            bytes((0,)) + NEUTRAL[:-1] + b"\x09"):
                response = usb_device_check._request(
                    port, CMD_TEST_GAMEPAD, sequence, payload, timeout
                )
                sequence += 1
                if response != bytes((1, 0, 0, 0, 0)):
                    raise ValueError("malformed gamepad diagnostic was not rejected")
            print("Malformed diagnostic commands rejected")
            # Kernel joystick state starts at zero until a report is received;
            # send fresh neutral reports after opening the input devices.
            for slot in range(2):
                request_report(port, slot, NEUTRAL, sequence, timeout)
                sequence += 1
            wait_for((NEUTRAL, NEUTRAL))
            try:
                for cycle in range(cycles):
                    for slot, report in enumerate(REPORTS):
                        request_report(port, slot, report, sequence, timeout)
                        sequence += 1
                        expected = [NEUTRAL, NEUTRAL]
                        expected[slot] = report
                        wait_for(expected)
                        wait_for((NEUTRAL, NEUTRAL))
                    print(f"Cycle {cycle + 1}/{cycles}: axes/buttons/hat, "
                          "slot isolation, and automatic release passed")
                for slot, report in enumerate(REPORTS):
                    request_report(port, slot, report, sequence, timeout)
                    sequence += 1
                wait_for(REPORTS)
                wait_for((NEUTRAL, NEUTRAL))
                print("Simultaneous input on both gamepads and automatic release passed")
            finally:
                for slot in range(2):
                    try:
                        request_report(port, slot, NEUTRAL, sequence, timeout)
                    except (OSError, RuntimeError, TimeoutError, ValueError):
                        pass
                    sequence += 1
            final_state = usb_device_check.read_device_state(
                port, sequence=sequence, timeout=timeout
            )
            if final_state != state:
                raise ValueError("device catalog changed during HID diagnostics")
            usb_ota.probe(port, retries=0, timeout=timeout)
            print(f"{'HID' if hid else 'CDC'} status/OTA probe still works; saved controller state unchanged")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=os.name != "nt")
    transport.add_argument("--port", help="legacy management CDC port")
    transport.add_argument("--hid", help="management HID node (or 'auto' on Windows) for audio firmware")
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--cycles", type=int, default=1)
    args = parser.parse_args()
    if args.timeout <= 0 or args.cycles <= 0:
        parser.error("timeout and cycles must be positive")
    use_hid = bool(args.hid) or (os.name == "nt" and not args.port)
    target = args.hid or args.port or "auto"
    try:
        check_gamepads(target, args.timeout, args.cycles, hid=use_hid)
    except (ImportError, OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"Gamepad check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
