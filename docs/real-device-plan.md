# Real-device validation and implementation plan

Prepared 2026-11-15 for the attached Ai-Thinker M62-12F-Kit.

## Objective and evidence

Establish a repeatable hardware baseline and recovery procedure, then implement
USB gamepad and radio/audio features only as their platform requirements pass.

Sources: [README](../README.md), [hardware findings](hardware.md),
[architecture](architecture.md), and [device-validation runbook](device-validation.md).
The build definitions and existing device checker were also reviewed.

## Execution progress

Implementation and bench verification began after the owner asked to execute
this plan. See the [current results](real-device-results.md) for evidence.

- Host suites and live CDC baseline: passed; browser hardware access remains unverified.
- USB backend: implemented, installed by native USB OTA, and verified through
  both Linux joystick devices, including simultaneous input and automatic release.
- RTOS timing: fixed a measured 320-fold clock mismatch; the live rate now
  matches the configured 1000 Hz tick rate.
- OTA/reboot: verified on the corrected build. Latest UART startup logs show
  FW slot 0 active after XZ OTA, superseding the earlier slot-1 assumption.
- WebHID: implemented, installed, and real feature-report commands and OTA
  verified. Browser UI tests pass with a simulated HID device. Direct browser
  hardware access needs the native device released from WSL to Windows;
  see [WebHID setup](webhid.md).
- Recovery buttons, a full device flash backup, visual LEDs, and deliberate
  update interruption: still pending physical bench work. Controlled OTA used
  the previously verified update path while those checks remained pending;
  a retained old firmware package is not a full flash backup.
- Radio: installed SDK already includes the DS5Dongle fork's HID pool patches.
  Its BL616 controller library documents two Classic ACL connections. Target
  device models and acceptance of separate operating modes still need owner input.
- Bluetooth pairing/controller translation and end-to-end audio: not implemented.

Both `/dev/ttyACM0` and `/dev/ttyUSB0` were confirmed present outside the
execution sandbox. The sandbox hides these nodes; live commands may require
execution outside it and appropriate serial-port permissions. No serial
connection, firmware upload, reset, or live protocol test was performed while
preparing this plan.

| Interface | Documented purpose | Planned use |
| --- | --- | --- |
| `/dev/ttyACM0` | Native USB CDC-ACM through J3 D+/D- | Status, non-writing protocol probe, browser configuration, authorized OTA |
| `/dev/ttyUSB0` | CH340 USB-UART bridge | Passive UART0 logs at 115200 baud; ROM inspection/recovery after verifying ROM entry |

Port presence does not establish board identity or application health. Earlier
documentation records a successful OTA into FW slot 1, a 21-byte status reply,
and a host-reported 480 Mbps link. Revalidate these observations before relying
on them. The status reply does not report the active boot slot.

## 1. Establish the current baseline

1. Identify USB VID/PID, serial identifiers where available, and the sysfs
   device paths for both ports. Check permissions and existing port users;
   close competing serial clients before connecting. Resolve the native USB
   device's actual sysfs speed path rather than assuming its old bus address.
2. Run the existing host suites from the project root:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   python3 -m unittest discover -s tests -p 'test_*.py' -v
   node --test tests/test_web_protocol.mjs
   ```

3. Run the existing non-writing hardware checker:

   ```sh
   python3 tools/usb_device_check.py --port /dev/ttyACM0
   ```

   This already includes the OTA unknown-command probe. Use the standalone
   `usb_ota.py --probe` command only for focused diagnosis if needed. Expected
   current behavior is a valid 21-byte state response, no advertised Bluetooth
   HID/gamepad/audio capabilities, explicit unsupported discovery/pairing,
   and unchanged state after an empty-slot selection attempt. Investigate
   differences rather than overwriting the running image to match expectations.
4. If diagnosis is needed, capture UART logs without transmitting or toggling
   reset/control lines. Opening a UART console does not require ROM mode;
   ROM flash inspection does. Initial boot messages require a later deliberate
   reset with the operator present.
5. Serve the browser panel and verify status through WebHID or Web Serial in a
   browser with local device access. Close the CLI port first. Verify capability
   gating, disconnect/reconnect behavior, and understandable errors without
   selecting/removing saved controllers or submitting OTA.

Exit: all available host checks pass, current device status/probe results and
USB identity/speed are recorded, and any browser access limitation is explicit.
Record command output and timestamps in a validation report; do not infer
firmware identity or active slot from port enumeration alone.

## 2. Make recovery reproducible

1. Establish the exact PROG/RST ROM-entry sequence from authoritative board
   information and bench observation with the operator. Do not guess timing.
2. In confirmed ROM mode, read the flash identity and both partition-table
   copies. Verify CRCs, active slot, addresses, and capacity against the
   documented 4 MiB layout. Read and hash a full current flash backup before
   ROM programming or deliberate interruption tests; preserve it outside generated build output.
   This backup cannot recover manufacturing contents already overwritten.
3. Record the loader/tool version, commands, flash configuration, button
   sequence, and how to return to the running application. Do not treat the
   loader's default flash configuration as validated for programming.
4. Verify repeatable ROM entry and normal boot, then rerun the CDC checker.
   Document a concrete restoration procedure for the verified layout; a
   destructive restore rehearsal requires a separate authorized bench test.

Exit: verified backup, readable tables, repeatable ROM access and application
boot, and a documented restoration procedure. Interrupted-update testing remains
blocked until restoration can be performed confidently.

## 3. Rebuild and validate controlled updates

1. Use the SDK/toolchain paths in the [runbook](device-validation.md) and build
   only `BOARD=m62_12f`. Record SDK/toolchain versions, build options, image
   hashes, package size, and linked RAM/flash use. Keep the current working
   image and baseline report available for comparison.
2. Before an upload, identify this kit and confirm its live slot/table state.
   The owner authorized implementation and real-device testing for this kit. The
   documented owner acceptance of lost `mfg` contents applies only to this kit.
   Determine the actual inactive/staging destination. The SDK writer switches
   metadata, but final post-XZ-OTA startup logs show slot 0 active; verify the
   resulting boot state rather than assuming successive updates alternate it.
3. Update through native CDC using the SDK-generated XZ OTA package. Capture
   UART logs concurrently. Have the operator check solid white while running
   and the red/green/blue chase during the update.
4. After reboot, reacquire the CDC device by identity if its port changes,
   rerun the checker, and check startup/storage logs and saved state. Verify
   the boot-slot transition through trusted boot logs or table readback;
   the existing state command cannot prove it.
5. After recovery is established, schedule separate interruption/rollback
   tests with defined cut points, expected old/new image behavior, and a
   restoration procedure. Do not interrupt the first baseline upload.

Exit: reproducible M62 build, successful authorized update and reboot, verified
slot transition, recorded LED observations, and separately evidenced recovery
and rollback results.

The existing `flash-app-m62` target writes fixed slot 0 at `0x10000` and does
not switch boot metadata. Do not use it as the normal updater. Generic SDK
flashing and the `bl616dk` reference image remain excluded for this kit.

## 4. Resolve Bluetooth feasibility before radio implementation

This investigation can start after the baseline, alongside recovery work.

1. Verify exact M62/BL616 silicon and supported SDK/controller-stack profiles.
   Require evidence of Bluetooth Classic HID host and A2DP source support,
   usable APIs, compatible licensing, and board RF calibration support.
   BLE 5.3 advertising and an SDK A2DP example are insufficient evidence.
2. Determine whether two controller links plus a headset link are supported.
   Measure usable heap, task stack margins, and USB/radio buffer needs before
   committing to simultaneous operation. The current M62 build rejects RF;
   resolve that board integration gate before enabling a radio backend.
3. Record a platform decision. If required Classic profiles are unavailable,
   propose a supported hardware/stack alternative before implementing pairing
   or promising headset support.

Exit: an evidence-backed platform decision and resource budget. Keep
capabilities unavailable until real backends pass bench tests.

## 5. Implement and validate gamepad behavior

1. Add a USB HID device backend for the portable router, retaining CDC for
   configuration/update. Validate descriptors and two distinct gamepad
   interfaces with deterministic synthetic reports first.
2. After the radio gate passes, connect a supported HID-host backend. Validate
   one controller, then two: discovery, pairing, reconnect, report fidelity,
   fixed slot-to-gamepad mapping, and unplug/disconnect behavior.
3. Validate persistent pair records and selection/removal across reset,
   reconnect, and interrupted settings saves using controlled test records.
4. Enable browser discovery/pairing only when the backend is operational;
   verify CLI/browser behavior against the same protocol and real devices.

Exit: two physical controllers independently operate their assigned host
gamepads; saved state and reconnect behavior are repeatable; CDC still works.

## 6. Add audio and test combined load

1. Confirm the intended USB audio topology and headset/controller profiles.
   Define supported formats and latency targets before connecting the existing
   PCM buffer to USB audio and a verified Bluetooth audio backend.
2. Validate audio alone, then alongside both controllers. Measure underruns,
   overruns, latency, heap/stack headroom, reconnect behavior, and USB stability.
3. Add controller speaker/microphone paths only if their actual protocols and
   resource measurements support them. Advertise only proven capabilities.
4. Complete repeated reconnect, power-cycle, update, and sustained-load runs;
   update the runbook with commands, observations, limitations, and results.

Exit: measured audio performance under combined load, stable controller/CDC
operation, and documented recovery/update behavior for the final configuration.

## Immediate next action

Resolve the pending controller/headset models and Classic connection-mode
decision, then implement the radio adapter and controller report translation.
Physical recovery/LED observations and interruption tests remain pending;
the USB implementation and its current bench results are already recorded.
