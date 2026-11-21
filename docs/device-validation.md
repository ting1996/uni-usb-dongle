# M62 device validation runbook

This runbook is for Codex or a developer working in this workspace with the
Ai-Thinker M62-12F-Kit attached. It separates safe, repeatable checks from
operations that write flash or require physical observation.

## Current attached device

- Native USB CDC-ACM: currently `/dev/ttyACM0`, connected through the M62's native USB
  lines on J3 pins 10 (D+) and 9 (D-).
- Prefer `/dev/serial/by-id/usb-Uni_USB_Dongle_Dual_Gamepad_+_Native_USB_OTA_00000001-if00`
  after OTA, because the ttyACM number can change.
- USB-UART: `/dev/ttyUSB0` through the CH340 bridge; use it only after
  confirming ROM download mode.
- The running dual-gamepad image was installed by native USB OTA. Latest UART
  startup logs show FW slot 0 active after XZ OTA; this supersedes the earlier
  slot-1 assumption. FW slot 1 aliases `mfg` at `0x210000` and can be used for
  staging. The owner accepted loss of the
  manufacturing image; it was not backed up. Do not generalize that consent
  to another board.
- WSL USB/IP reports the device at 480 Mbps. HID/CDC operation has been checked
  separately; this link speed does not validate audio operation.
- The firmware reports USB gamepad capability (`0x02`). Two HID interfaces
  coexist with CDC at 480 Mbps; both passed synthetic-input bench tests.
  Bluetooth HID and audio remain unavailable; discovery/pairing return `UNSUPPORTED`.

## Safe checks (no flash writes)

Run from the repository root:

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 tools/usb_device_check.py --port /dev/ttyACM0
python3 tools/usb_ota.py --port /dev/ttyACM0 --probe
```

`usb_device_check.py` reads device state, confirms discovery/pairing are
explicitly unsupported when the Bluetooth HID capability is absent, verifies
that selecting an empty controller slot does not change state, and runs the
unknown-command OTA probe. It does not begin an update. If a future firmware
advertises Bluetooth HID, the checker skips discovery/pairing to avoid
starting radio actions.

To confirm host-observed USB speed in this WSL environment:

```sh
cat /sys/devices/platform/vhci_hcd.0/usb1/1-1/speed
lsusb -t
```

The current host reports `480` Mbps / `480M`.

## Explicit USB gamepad diagnostics (input events, no flash writes)

Close applications that could react to gamepad input. The diagnostic command
sends synthetic axes/buttons/hat values and the firmware releases them after
500 ms. It does not create pair records. On this Linux host the default checker
reads the accessible joystick nodes, without root access or detaching drivers:

```sh
python3 tools/usb_gamepad_check.py \
  --port '/dev/serial/by-id/usb-Uni_USB_Dongle_Dual_Gamepad_+_Native_USB_OTA_00000001-if00' \
  --cycles 10
```

The checker resolves input nodes through the CDC device's sysfs identity,
applies the kernel's read-only joystick calibration, checks the 1000 Hz RTOS
clock, both report paths, slot isolation, automatic release, simultaneous input,
unchanged saved state, and continuing CDC status/OTA probe operation.
Raw USB access was unavailable under the host's permissions, so the checker
uses the kernel joystick path. No driver detachment or permission change is needed.

The SDK port derives timer increments from `configCPU_CLOCK_HZ`, which must be
`1000000UL` for the board's 1 MHz MTIME clock, despite the 320 MHz CPU speed.
The old value stretched RTOS delays by about 320 times. Host stubs cannot
detect this mismatch; retain the live tick-rate and release checks.

## Build and update

WebHID feature-report checks, the verified HID OTA path, and browser setup are
documented in [WebHID configuration and OTA](webhid.md). The management interface
is currently `/dev/hidraw0`; it requires read/write access or an authorized root
test process, unlike the read-only joystick checks above.

The SDK and toolchain used in this workspace are:

```sh
export BL_SDK_BASE=/mnt/d/Workspace/bouffalo_sdk
export TOOLCHAIN_BIN=/mnt/d/Workspace/Xuantie-900-gcc-elf-newlib-x86_64-V3.2.0/bin
export PATH="$TOOLCHAIN_BIN:$PATH"
```

Build the M62 profile:

```sh
make -C firmware BOARD=m62_12f \
  CROSS_COMPILE="$TOOLCHAIN_BIN/riscv64-unknown-elf-"
```

Before any OTA write, confirm the target is the intended device and that loss
of its FW slot 1/`mfg` contents is still acceptable. The user explicitly
accepted that loss for the currently attached kit. A future Codex run must
not assume this applies to another device.

The non-writing probe is safe to run at any time:

```sh
python3 tools/usb_ota.py --port /dev/ttyACM0 --probe
```

Only with explicit authorization to write the aliased slot:

```sh
python3 tools/usb_ota.py \
  --port /dev/ttyACM0 \
  --image firmware/build/build_out/uni_usb_dongle_bl616.xz.ota
```

Keep the device powered and attached until the tool reports success. After a
successful update, rerun `usb_device_check.py` to verify reboot and protocol
state. The OTA image hash is an integrity check, not a firmware signature.
Interrupted-update rollback has not been tested.

## Fixed-address UART programming is not the normal updater

`make -C firmware BOARD=m62_12f flash-app-m62` writes only FW slot 0 at
`0x10000`; it does not inspect or switch partition-table boot metadata.
Latest post-XZ-OTA logs show slot 0 active, so the older statement that it
would write an inactive slot is no longer current. Verify the live table
before deliberate ROM programming. The exact PROG/RST sequence for ROM
download mode is not documented in this workspace; the official Zephyr board
instructions only describe pressing RST to run an application. Do not guess
the button sequence or start UART flashing until ROM entry is established.

## Remaining bench-only checks

- Visually confirm the solid white run LED and red/green/blue OTA chase.
- Establish and record the board's ROM download button sequence.
- Verify interrupted-update rollback only after a recovery path is available.
- Bluetooth HID and end-to-end audio need a supported M62 backend and actual
  controllers/headsets; the current firmware reports those capabilities as
  unavailable.
