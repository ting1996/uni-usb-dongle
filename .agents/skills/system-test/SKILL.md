---
name: system-test
description: >-
  Run real-hardware system tests on the attached M62 USB dongle (HID, gamepad,
  USB audio, and Bluetooth). Checks for at least 1 Bluetooth Headphone and
  1 Bluetooth Controller; if not enough devices are found, MUST stop and ask
  the user to prepare them and approve before continuing the current task.
---

# Hardware System Test (Dongle + Bluetooth Headphone & Controller)

Use this skill when validating real M62-12F-Kit hardware behavior across USB HID, gamepads, UAC1 audio, and Bluetooth Classic links.

## Prerequisite: Minimum Bluetooth Devices

A full system test requires **at least 2 Bluetooth devices**:
- **1 Bluetooth Headphone** (e.g., Sony BRAVIA Theatre U / A2DP audio sink)
- **1 Bluetooth Controller** (e.g., Sony PS5 DualSense / Wireless Controller)

*(Remember the BL616 controller limit: at most 2 simultaneous Classic ACL links — 1 controller + 1 headphone, or 2 controllers.)*

## Step 1: Dongle & Host Pre-Flight

1. Confirm the M62 native USB dongle is attached to the active host OS:
   - On Windows, if the dongle is currently bound to WSL2 via `usbipd`, it must be detached back to Windows (`usbipd list` / `usbipd detach --busid <bus-id>`) before Windows HID/`winmm.dll` scripts can open it.
   - Note: Windows kernel HID (`\\?\hid#...`) and audio checks require running outside the restricted terminal sandbox.
2. If you want UART console logs during the test, start `tools/uart_monitor.py --port <COMx> --duration <sec>` **before** opening HID or audio clients (opening the CH340 port can briefly reset the board).
3. Verify the management HID interface responds:
   ```powershell
   python tools/usb_hid_check.py --device auto
   ```
   *(On Linux/WSL, pass `--device /dev/hidraw0` or the resolved hidraw node.)*

## Step 2: Check Bluetooth Device Availability (>= 1 Headphone AND >= 1 Controller)

1. Query live Bluetooth radio status and paired controller/headphone slots:
   ```powershell
   python tools/bluetooth_check.py --device auto
   ```
   Inspect the JSON output:
   - **Controller ready**: `controllers` bitmask is non-zero (`!= 0`), and `device_state` shows a paired controller slot.
   - **Headphone ready**: `headphone` is non-zero (`!= "00:00:00:00:00:00"`), `audio_connected` is `true`, `audio_authenticated` is `true`, and `audio_state` is `4` (A2DP connected) or `5` (streaming).
2. If either the Headphone or the Controller is not yet connected/authenticated, run a Bluetooth discovery scan:
   ```powershell
   python tools/bluetooth_check.py --device auto --scan
   ```
   Check the combined set of connected/paired and discovered devices to confirm whether **at least 1 Bluetooth Headphone** and **at least 1 Bluetooth Controller** are available to test.

## Step 3: MANDATORY STOP GATE — Ask User to Prepare Missing Bluetooth Devices

**If you cannot find or connect at least 1 Bluetooth Headphone AND at least 1 Bluetooth Controller, STOP IMMEDIATELY.**

1. Report clearly to the user:
   - Current `radio_state` and `device_state` summary (`controllers` mask, `headphone`, `audio_state`, `audio_authenticated`).
   - Any devices discovered by `--scan`.
   - Exactly what is missing (e.g., `Missing: 1 Bluetooth Controller` and/or `Missing: 1 Bluetooth Headphone`).
2. Ask the user to prepare the missing device(s):
   - **Bluetooth Controller** (e.g., DualSense): press the **PS** button to reconnect a paired controller, or hold **Create + PS** until the lightbar flashes to enter pairing mode.
   - **Bluetooth Headphone** (e.g., Sony Theatre U): power it on to auto-reconnect, or hold **Power/Bluetooth** until pairing mode starts.
3. Ask the user to reply and approve once both devices (1 Headphone + 1 Controller) are powered on / in range / ready.

**End your turn and wait for user approval.** Do not fabricate results or bypass the Bluetooth readiness check.

### Resuming After User Approves

1. Re-run `python tools/bluetooth_check.py --device auto`.
2. If a newly prepared device is in pairing mode and not yet paired, run:
   - `python tools/bluetooth_check.py --device auto --scan`
   - Pair controller (`--pair-controller AA:BB:CC:DD:EE:FF --wait 10`) and/or headphone (`--pair-audio AA:BB:CC:DD:EE:FF --wait 10`) using the discovered addresses.
3. Verify again that **at least 1 Headphone** (`audio_authenticated == true`) and **at least 1 Controller** (`controllers != 0`) are connected/available. If still missing, stop and inform the user again.

## Step 4: Execute System Test Suite

Once both Bluetooth devices (1 Headphone + 1 Controller) are confirmed ready:

1. **Windows Kernel Hardware Suite** (management HID, dual gamepads, silent USB audio, radio check):
   ```powershell
   .\tools\windows.ps1 test -Suite Hardware
   ```
   *(Caution: `usb_gamepad_check.py` injects synthetic axes/buttons/hat cycles for 500 ms; ensure no one is holding physical controller sticks/buttons during that check.)*
2. **Bluetooth Headphone Tone + PCM / SBC Verification**:
   ```powershell
   python tools/usb_audio_check.py --device auto --seconds 3 --tone --amplitude 8000
   ```
   Verify:
   - USB PCM bytes received within tolerance (`44100 * 4 * seconds`).
   - Bluetooth PCM bytes submitted `>= 90%` of expected (`before["audio_authenticated"]` is true).
   - Zero new `overflows` or `audio_errors`, and `nonzero_packets` incremented.
   - Optional deep SBC check: add `--capture build\windows\sbc-capture.bin` and verify SBC RTP + CRC-8 with `--inspect-capture`.
   - Optional USB mic check (Windows `waveIn`): `python tools/usb_audio_check.py --device auto --seconds 2 --mic`.
3. **Concurrent Controller + Headphone Link Stability**:
   ```powershell
   python tools/bluetooth_check.py --device auto --wait 5
   ```
   Confirm `controllers != 0`, `audio_authenticated == true`, `audio_state` in `(4, 5)`, and zero new underflows/overflows/errors.
4. **Output Profile & DualSense Telemetry (when relevant to the task)**:
   ```powershell
   python tools/usb_hid_check.py --device auto --get-profile
   python tools/usb_hid_check.py --device auto --dualsense-metrics 0
   ```
   If testing `--set-profile ps4` / `xinput` / `switch` / `dualsense`, switch back to `generic` afterward if needed.

## Step 5: Report Results & Continue Current Task

- Report exact pass/fail status for each hardware check, measured PCM/BT/radio counters, and report artifact paths (`build\windows\test-results\test-report.json`, `test-report.xml`).
- Clearly call out any checks that still depend on physical user confirmation (audible 440 Hz tone in the headphone, physical RGB LED colors, physical controller stick/button presses).
- **Continue the remaining steps of the current task** (e.g., review, documentation update, or GitHub push).
