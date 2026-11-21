# Current Bluetooth/audio hardware results

The owner flashed the radio/audio firmware on November 15. Bluetooth startup,
inquiry, HID management, both USB gamepad diagnostics, the 1 kHz timer and USB
stereo PCM reception now pass on real hardware. DualSense pairing/connection and
Theater U authentication with saved pairing were subsequently observed. Live
controller input, audible A2DP playback and reconnect remain pending. A November
16 update fixes a connection-timeout assertion and adds saved-device retries.
HID OTA and recovery from a real page timeout passed; Theater U automatically
authenticates and connects its signaling link; SBC streaming and PCM delivery
tests subsequently passed. Audible confirmation and live controller input remain
pending. See
[current Bluetooth/audio results](bluetooth-audio.md) for details and hashes.

The earlier USB disappearance was caused by the UART logger's default DTR/RTS
control-line settings; deasserting both before opening the port restored stable
boot and USB operation. Use [uart_monitor.py](../tools/uart_monitor.py).

The remainder of this report describes the historical USB-only baseline, saved
in [backups/pre-radio](../backups/pre-radio/); its hashes do not identify the
current radio/audio build.

# M62 real-device implementation and verification

Date: 2026-11-15. Scope: the owner's attached Ai-Thinker M62-12F-Kit.

## Delivered and installed

- Two native USB HID gamepad interfaces alongside the existing CDC configuration
  and OTA transport. Both enumerate as Linux joystick devices.
- A normalized nine-byte input backend usable as the portable router's sink:
  four stick axes, two triggers, 16 buttons, and a hat switch. Bluetooth report
  translation still needs a controller-specific adapter.
- Explicit CDC gamepad diagnostics with automatic input release after 500 ms;
  diagnostic inputs do not create pairing records or change saved selection.
- A corrected FreeRTOS timer configuration and a repeatable Linux bench checker.
- WebHID management and OTA using vendor feature report 2 on endpoint 0,
  alongside input report 1. Web Serial remains a fallback. See the
  [WebHID setup and protocol](webhid.md) for browser access and verification limits.

The firmware was built with `BOARD=m62_12f` against the installed Bouffalo SDK
2.3.32 (`sqlCRT/bouffalo_sdk` remote) and T-Head GCC 14.1.1. It was installed
through CDC, then a full repeat upload through HID feature reports. The final startup log identifies
build time `22:07:27, Nov 15 2026`, SDK component revision `b3532953`, Boot2
8.1.8, BL616 A0/QFN40, 4 MiB flash, and JEDEC `C8 60 16`.

| Build artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Application binary | 120896 | `49c72e1e393157401fd36740cc8602b75b54e4f6e13a3463f02f0ba79769a79e` |
| XZ OTA package | 69088 | `c3f129763adfb0c779f05ea4fb983b27b78ac5c8757f26f279c42e42b1b4d57e` |

Use the SDK-generated package at
`firmware/build/build_out/uni_usb_dongle_bl616.xz.ota` for this CDC updater.
The application binary is not an OTA package. Neither artifact is a full flash
backup. The previous application/package were retained under
`backups/pre-usb-hid/` before replacing the build output.

## Verification evidence

| Check | Result |
| --- | --- |
| C build and CTest | 7 tests passed, including the feature transport and real USB backend code against deterministic RTOS/USB stubs |
| Python host tests | 16 tests passed |
| JavaScript protocol tests | 10 tests passed using the existing VS Code Node runtime |
| Native USB update | Final package accepted through CDC and then through HID; reboot observed over UART |
| USB enumeration | CDC interfaces 0/1 and HID interfaces 2/3; host reports 480 Mbps |
| HID descriptors | Interface 2 exposes the 100-byte input/vendor-feature descriptor; interface 3 exposes the 77-byte input descriptor |
| Device state | Valid 21-byte response, capabilities `0x02` (USB gamepad), selection `255`, both controller slots empty |
| Individual gamepads | Initial backend passed ten cycles; WebHID build passed two cycles plus a post-HID-OTA recheck for both slots, including axes/buttons/hat, isolation and release |
| Simultaneous inputs | Both gamepads accepted distinct test input together, then independently returned to neutral |
| RTOS clock | Approximately 997–1001 ticks/second measured against host elapsed time; expected 1000 |
| Malformed diagnostics | Empty payload, invalid slot, and invalid hat value rejected without applying input |
| CDC after HID tests | Status and non-writing OTA probe passed; saved controller state unchanged |
| Unsupported radio operations | Discovery/pairing explicitly return unsupported; empty-slot selection is rejected without changing state |
| HID management | Real status/probe and maximum-size request fragmentation passed through hidraw; state unchanged |
| OTA transport isolation | During live HID OTA, CDC status worked; CDC abort/selection returned busy; HID OTA completed and rebooted |
| Browser UI | Simulated-device connect, status, capability gating, OTA, and disconnect passed in Chromium; native browser/device access still pending |

The input checker reads actual `/dev/input/js*` event streams from the kit. It
resolves them through the CDC device's sysfs ancestry, checks kernel axis maps,
and applies the read-only joystick calibration before comparing values. The
tests therefore cover the physical USB transport and Linux HID/input handling,
in addition to firmware state. They use synthetic reports, not physical
Bluetooth controller input.

## Timing defect found on hardware

The original application set `configCPU_CLOCK_HZ` to 320 MHz. In this SDK's
RISC-V FreeRTOS port that macro sets MTIME tick increments, while the reused
BL616 startup actually configures MTIME at 1 MHz. Device diagnostics measured
only about 3.125 ticks per second instead of 1000. Consequently a nominal
500 ms test release took approximately 160 seconds; OTA timeout and reset
delays were stretched too.

The application now uses `1000000UL`, matching the SDK's reference FreeRTOS
applications. Real-device tick measurements and automatic-release tests pass.
Host RTOS stubs alone cannot detect a mismatch with the physical timer clock.
The requirement is also recorded in the repository's firmware instructions.

## Boot-slot evidence and recovery limits

The final UART table dump reports FW `active_index = 0`, address `0x10000`,
table age 27, and FW entry age 16. This supersedes earlier documentation that
assumed FW slot 1 remained active after XZ OTA. The SDK writer's inactive-slot
metadata change alone does not prove the final post-boot running slot.

FW slot 1 still overlaps `mfg` at `0x210000`. The owner previously accepted
loss of that image on this kit. Controlled OTA continued using the proven
update path while physical recovery checks remained pending; no full current
flash backup or deliberate interruption test was performed. The fixed-address
UART target still writes only slot 0 and does not inspect/switch boot metadata.
No UART programming, Boot2 replacement, or ROM button procedure was attempted.

CDC's existing notification endpoint was not exercised by these tests. The SDK
BL616 USB driver services endpoint numbers 1–4 and ignores higher numbers,
while the inherited CDC descriptor advertises notification endpoint `0x85`.
The verified traffic uses HID IN `0x81/0x82`, CDC IN `0x83`, and CDC OUT `0x04`.
Any future composite audio layout must account for this physical FIFO budget;
these results do not establish full CDC notification or USB class compliance.

## Remaining work and required bench inputs

1. Confirm controller models and headset model, and make them available for
   pairing and real input/audio tests. Bluetooth discovery, HID-host behavior,
   report translation, reconnect, and link-key persistence remain unimplemented.
2. Decide the radio concurrency requirement. The installed SDK is already the
   DS5Dongle fork and registers HID TX/RX pools. Its
   `components/wireless/bluetooth/BLUETOOTH-LIBS.md` documents **two Classic ACL
   connections** for the BL616 BR/EDR library. Two Classic controllers plus a
   separate Classic headset need three: separate two-controller and
   controller-plus-headset modes, or a different controller stack/hardware,
   require an explicit product decision.
3. Verify the M62 RF startup/calibration path before enabling radio code;
   the present board profile rejects RF builds. Measure heap/stack headroom
   with the selected stack rather than treating the linker map as free heap.
4. Implement and measure end-to-end audio after the radio/topology decision.
   No Bluetooth audio or USB audio path is currently advertised or verified.
5. With physical help, record ROM entry/normal boot, obtain a full flash backup,
   check run/OTA LEDs, and then validate interrupted-update recovery.
6. Verify direct WebHID/serial browser operations with an authorized local browser.
   Hardware HID feature reports and OTA pass, and the browser UI passes with a
   simulated device. The native USB device must be released from WSL to Windows
   for the Windows browser test; see [WebHID setup](webhid.md).

## Repeat the verified checks

Close competing serial clients and applications that could react to gamepad
input. The CDC tty number changed during updates, so use its stable identity:

```sh
python3 tools/usb_device_check.py \
  --port '/dev/serial/by-id/usb-Uni_USB_Dongle_Dual_Gamepad_+_Native_USB_OTA_00000001-if00'
python3 tools/usb_gamepad_check.py \
  --port '/dev/serial/by-id/usb-Uni_USB_Dongle_Dual_Gamepad_+_Native_USB_OTA_00000001-if00' \
  --cycles 10
```

The gamepad checker requires pyserial and read access to the Linux joystick
nodes; it does not require raw USB access, root permissions, or driver detachment.
Protocol `0x15` sends the slot plus nine normalized report bytes. Read-only
protocol `0x16` returns status, flags, the current report, queued/completed
transfer counts, current ticks, and test-start ticks (27 bytes total).
