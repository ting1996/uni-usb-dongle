#!/usr/bin/env python3
"""Run non-destructive device checks over native USB CDC-ACM."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import sys
import time

if __package__:
    from . import usb_ota
else:
    import usb_ota


CMD_GET_DEVICE_STATE = 0x10
CMD_SELECT_CONTROLLER = 0x11
CMD_START_DISCOVERY = 0x13
CMD_PAIR_CONTROLLER = 0x14

STATUS_OK = 0
STATUS_UNSUPPORTED = 8
STATUS_NOT_FOUND = 10

CAPABILITY_BLUETOOTH_HID = 0x01
CAPABILITY_USB_GAMEPAD = 0x02
CAPABILITY_AUDIO = 0x04
NO_SELECTED_SLOT = 0xFF
CONTROLLER_SLOT_COUNT = 2
DEVICE_STATE_SIZE = 21


@dataclass(frozen=True)
class ControllerState:
    slot: int
    paired: bool
    connected: bool
    gamepad_index: int
    address: str


@dataclass(frozen=True)
class DeviceState:
    capabilities: int
    selected_slot: int
    controllers: tuple[ControllerState, ...]


def _request(port, command, sequence, payload, timeout):
    port.write(usb_ota.encode_frame(command, sequence, payload))
    port.flush()
    response_command, response_sequence, response_payload = usb_ota.read_response(
        port, timeout
    )
    if (response_command != (command | usb_ota.RESPONSE_FLAG) or
            response_sequence != sequence):
        raise ValueError(
            f"unexpected response to command {command}: "
            f"command={response_command}, sequence={response_sequence}"
        )
    return response_payload


def read_device_state(port, sequence=0, timeout=3.0):
    payload = _request(port, CMD_GET_DEVICE_STATE, sequence, b"", timeout)
    if len(payload) != DEVICE_STATE_SIZE or payload[0] != STATUS_OK:
        raise ValueError(
            f"invalid device-state response: length={len(payload)}, "
            f"status={payload[0] if payload else 'missing'}"
        )

    controllers = []
    for slot in range(CONTROLLER_SLOT_COUNT):
        offset = 3 + slot * 9
        paired = payload[offset]
        connected = payload[offset + 1]
        if paired not in (0, 1) or connected not in (0, 1):
            raise ValueError(f"invalid controller state for slot {slot}")
        address = ":".join(
            f"{byte:02x}" for byte in payload[offset + 3:offset + 9]
        )
        controllers.append(
            ControllerState(
                slot=slot,
                paired=bool(paired),
                connected=bool(connected),
                gamepad_index=payload[offset + 2],
                address=address,
            )
        )

    selected_slot = payload[2]
    if selected_slot != NO_SELECTED_SLOT and selected_slot >= len(controllers):
        raise ValueError(f"invalid selected controller slot: {selected_slot}")

    return DeviceState(
        capabilities=payload[1],
        selected_slot=selected_slot,
        controllers=tuple(controllers),
    )


def check_device(port, timeout=3.0, output=print):
    state = read_device_state(port, timeout=timeout)
    capabilities = []
    for bit, name in (
        (CAPABILITY_BLUETOOTH_HID, "Bluetooth HID"),
        (CAPABILITY_USB_GAMEPAD, "USB gamepad"),
        (CAPABILITY_AUDIO, "audio"),
    ):
        if state.capabilities & bit:
            capabilities.append(name)
    capability_summary = ", ".join(capabilities) if capabilities else "none"
    output(
        f"Device state: capabilities=0x{state.capabilities:02x} "
        f"({capability_summary}), "
        f"selected={state.selected_slot}"
    )
    for controller in state.controllers:
        status = "paired" if controller.paired else "empty"
        if controller.connected:
            status += ", connected"
        output(
            f"  slot {controller.slot + 1}: {status}, "
            f"gamepad={controller.gamepad_index}, address={controller.address}"
        )

    next_sequence = 1
    if not state.capabilities & CAPABILITY_BLUETOOTH_HID:
        for command, name in (
            (CMD_START_DISCOVERY, "discovery"),
            (CMD_PAIR_CONTROLLER, "pairing"),
        ):
            response = _request(port, command, next_sequence, b"", timeout)
            next_sequence += 1
            if response != bytes((STATUS_UNSUPPORTED,)):
                raise ValueError(
                    f"{name} should be explicitly unsupported when Bluetooth "
                    f"HID is unavailable; got {response.hex()}"
                )
            output(f"{name}: explicitly unsupported")
    else:
        output("Bluetooth HID is available; skipped radio actions")

    empty_slot = next(
        (controller for controller in state.controllers if not controller.paired),
        None,
    )
    if empty_slot is not None:
        response = _request(
            port,
            CMD_SELECT_CONTROLLER,
            next_sequence,
            bytes((empty_slot.slot,)),
            timeout,
        )
        next_sequence += 1
        if response != bytes((STATUS_NOT_FOUND, state.selected_slot)):
            raise ValueError(
                f"empty-slot selection returned unexpected response "
                f"{response.hex()}"
            )
        updated_state = read_device_state(port, next_sequence, timeout)
        if updated_state != state:
            raise ValueError("device state changed after empty-slot selection")
        output("empty-slot selection: rejected without changing state")
    else:
        output("both controller slots are paired; skipped selection check")

    usb_ota.probe(port, retries=0, timeout=timeout)
    output("OTA protocol probe: passed (no update or flash writes)")
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        required=True,
        help="native USB CDC-ACM port, e.g. /dev/ttyACM0 or COM5",
    )
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    try:
        import serial
    except ImportError as error:
        raise SystemExit(
            "Install pyserial first: python3 -m pip install pyserial"
        ) from error

    try:
        with serial.Serial(
            args.port, baudrate=115200, timeout=0.1, write_timeout=5
        ) as port:
            port.dtr = False
            port.rts = False
            time.sleep(0.25)
            check_device(port, timeout=args.timeout)
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"Device check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
