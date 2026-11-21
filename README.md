# Bluetooth/audio & USB output profile status

The radio/audio firmware now includes **USB output profile switching**
(`Generic`, `XInput`, `PS4`, `Switch`, and `DualSense`), separated **WebHID management on
Interface 2**, **AVRCP Target (`CONFIG_BT_AVRCP=y`)** for headphone control
and absolute volume, live **PCM peak / AVRCP telemetry (`0x20`)**, and a
**Windows UART flash backup & BL616 partition-table CRC verifier**
(`.\tools\windows.ps1 backup` + `tools/bl616_pt_check.py`).
See [Bluetooth/audio setup and verified results](docs/bluetooth-audio.md) and the
[Windows native build/test/flash guide](docs/windows.md).

RGB connection status uses 50% hardware PWM: red for controller slot 1, green
for controller slot 2, and blue for the authenticated headphone link. OTA keeps
its temporary RGB chase. Audible headphone playback on Theater U is under active
verification with AVRCP Target and increased tone amplitude diagnostics.

The dongle exposes UAC1 44.1 kHz stereo playback (`MI_00`/`MI_01`), standalone
vendor management/OTA HID (`MI_02`), and dual controller interfaces (`MI_03`/`MI_04`
for `Generic`, `XInput` `045e:028e`, `PS4` DualShock 4 `054c:09cc`, `Switch`
Pro `057e:2009`, or `DualSense` `054c:0ce6`). Profile changes via `0x1f` are session-only and
reset to `Generic` on reboot. The older pre-radio firmware remains in
[backups/pre-radio](backups/pre-radio/).

# USB dongle firmware scaffold

This project targets the Ai-Thinker M62-12F-Kit and uses FreeRTOS as the
firmware OS. It is an in-progress, host-testable scaffold, **not yet
kit-ready firmware**. See [hardware findings](docs/hardware.md) and
[architecture and implementation status](docs/architecture.md) for platform
constraints and stage details.

The [latest real-device results](docs/real-device-results.md) cover the
dual-gamepad USB backend, the corrected FreeRTOS timer, and current boot-slot
evidence. The [implementation plan](docs/real-device-plan.md) tracks the
remaining radio, audio, and recovery work.

## Current features

- Portable two-controller catalog with stable slot/gamepad-index mapping,
  pairing records, selection, and removal.
- Versioned, checksummed settings codec and Bouffalo SDK LittleFS/MTD settings
  store. The store uses the named `PSM` partition and avoids formatting a
  non-erased partition it cannot mount.
- Portable controller-report routing seam and output profile encoders
  (`output_profile.c` / `usb_profiles.c`). A paired slot's normalized report is
  translated for `Generic` (dual HID gamepads), `XInput` (`045e:028e`), `PS4`
  (`054c:09cc`, including feature reports `0x02`/`0x03`/`0x12`/`0xa3`),
  `Switch` (`057e:2009`, including handshake/subcommand/SPI replies), or
  `DualSense` (`054c:0ce6`, including raw 64B input passthrough, bidirectional
  `0x05`/`0x08`/`0x09`/`0x20`/`0x80`/`0x81` features with `0x53`/`0xA3` CRC-32,
  BT lightbar unlock + USB `0x02` → BT `0x31` output forwarding, and `0x21` telemetry), switched at
  runtime over USB management HID with safe BL616 shared-FIFO gating.
- Bounded PCM stream buffer plus UAC1 44.1 kHz stereo speaker playback (`EP 0x03`,
  Adaptive ISO) and 16 kHz mono USB microphone (`EP 0x84`, `MI_00..MI_02`, Mute/Volume Feature Unit
  with silence/ring-buffer feed until an HFP-AG/ADC voice source is added), SDK SBC/A2DP
  source, AVRCP Target (`CONFIG_BT_AVRCP=y`), live PCM peak counters (`0x20`),
  and pure-Python SBC RTP + CRC-8 packet validation in
  [tools/usb_audio_check.py](tools/usb_audio_check.py).
- Browser configuration page over WebHID (plus legacy Web Serial fallback),
  matching all five profile VID/PIDs on usage page `0xff00` usage `1` for
  device status, discovery, controller/headphone pairing, session output
  profile selection, AVRCP/PCM audio telemetry, and XZ OTA. See
  [WebHID setup](docs/webhid.md).
- Native Windows 10/11 CLI ([tools/windows.ps1](tools/windows.ps1)) for
  `doctor`, host/M62 firmware `build`, C/Python/Node/Hardware `test`,
  dry-run-gated `flash`, and read-only UART `backup` +
  [tools/bl616_pt_check.py](tools/bl616_pt_check.py) BFPT CRC-32 verification.
  See [Windows guide](docs/windows.md).

## Remaining hardware / bench TODOs

- Document the repeatable physical `PROG`/`RST` button sequence for entering
  BL616 UART ROM download mode and capture an on-device full/table flash backup
  with `.\tools\windows.ps1 backup -Device COM7`.
- Visually verify status LED colors on the M62 hardware (slot 1 red, slot 2
  green, headphone blue, OTA RGB chase) and exercise physical DualSense
  buttons/sticks end-to-end.
- Confirm audible headphone playback on Sony BRAVIA Theatre U with AVRCP Target
  enabled and validate `XInput` / `PS4` / `Switch` / `DualSense` profiles against target hosts.
- Test interrupted-OTA rollback behavior against dual-slot Boot2 metadata.

## Run host tests

Requirements: CMake 3.20+ and a C11 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

These commands build and run the portable core tests; they do not run the
dongle firmware.

Run the host-side OTA protocol tests with:

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

Browser transport tests use Node.js:

```sh
node --test tests/test_web_protocol.mjs tests/test_web_hid.mjs tests/test_web_radio.mjs
```

### Windows 10/11 (PowerShell, Native)

Use the unified PowerShell script for environment checks, native MSVC host and
Xuantie RISC-V firmware builds, automated + Windows kernel hardware tests (with
JUnit XML and JSON reports in `build\windows\test-results\`), validated flash
dry-runs, and cleanup:

```powershell
.\tools\windows.ps1 doctor
.\tools\windows.ps1 build -Target All -Config Release
.\tools\windows.ps1 test
.\tools\windows.ps1 test -Suite Hardware
.\tools\windows.ps1 flash -Method hid -DryRun
.\tools\windows.ps1 clean
```

See [Windows 10/11 native build, test, and flash guide](docs/windows.md) for
prerequisites, Windows kernel tools (`tools/usb_hid_check.py`,
`tools/usb_gamepad_check.py`, `tools/usb_audio_check.py`,
`tools/bluetooth_check.py`), driver notes, and troubleshooting.

For reproducible checks on the connected M62, including the safe live CDC
state/OTA probe, see [the device-validation runbook](docs/device-validation.md).

Serve the browser panel from a secure local origin (for example
`http://localhost:8000`) in desktop Chrome or Edge with WebHID support:

```sh
python3 -m http.server 8000 --directory web
```

Choose WebHID, click Connect, and select the dongle management interface (any of
the five output-profile VID/PIDs). Choose USB serial only as a fallback for
legacy CDC-ACM firmware. The page displays saved controller slots,
discovery/pairing controls, session output-profile selection, audio link /
AVRCP / PCM peak telemetry, and XZ OTA updates (gated by an acknowledgement of
the FW slot 1/`mfg` overlap).

## Build the firmware

Requirements: the Bouffalo SDK and its T-Head `riscv64-unknown-elf-` toolchain.
Set the paths for your installation, then run:

```sh
export BL_SDK_BASE=/path/to/bouffalo_sdk
export TOOLCHAIN_BIN=/path/to/toolchain/bin
export PATH="$TOOLCHAIN_BIN:$PATH"
make -C firmware BOARD=bl616dk \
  CROSS_COMPILE="$TOOLCHAIN_BIN/riscv64-unknown-elf-"
```

For the M62-12F-Kit profile:

```sh
make -C firmware BOARD=m62_12f \
  CROSS_COMPILE="$TOOLCHAIN_BIN/riscv64-unknown-elf-"
```

If the SDK is checked out at `../bouffalo_sdk`, `BL_SDK_BASE` may be omitted.
The SDK post-build tools require the toolchain `bin` directory on `PATH`.
Build output defaults to `firmware/build`; keep `BUILD_DIR` relative to
`firmware/` for the SDK linker script. The M62 profile reuses the SDK's
BL616 startup sources, omits board GPIO and RF calibration, disables PSRAM,
and keeps flash at the default low speed rather than applying BL616DK timing
calibration. Basic M62 startup has been verified after OTA. Board-specific RF
calibration and physical status LED behavior remain unverified.

`BOARD=bl616dk` remains a reference-board smoke configuration and must not be
flashed to the M62-12F-Kit. Generic `flash` is blocked for both profiles.

## Initial flashing/recovery over USB-UART

The kit's built-in USB connector exposes UART0 through its CH340 USB-to-UART
bridge. Use this path only for ROM recovery or deliberate slot-0 programming.
The app-only target always writes slot 0 without inspecting boot metadata.
Native USB OTA is enabled in the M62 profile. It uses the SDK A/B writer;
Boot2 is outside the writer's erase/write range, but the inactive slot
overlaps `mfg` and may erase manufacturing data.

The installed table and flash ID have now been read from the inspected kit
using the BL616 ROM downloader: JEDEC `C8 60 16`, 4 MiB, and two valid,
identical partition-table copies matching the SDK BL616DK reference layout.
The Boot2 range was also read: its image bytes match the SDK BL616 v8.1.8
release image, followed by erased padding. This does not make the BL616DK
application startup profile safe for the M62. The M62 profile has booted
successfully after native USB OTA, but a repeatable PROG/RST ROM-recovery
sequence is still undocumented. Do not flash the `BOARD=bl616dk` smoke image
or its reference Boot2/partition files to the kit. See
[hardware findings](docs/hardware.md) for the evidence and remaining checks.

The read-only table inspection used:

```sh
/mnt/d/Workspace/bouffalo_sdk/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu \
  --interface=uart --port=/dev/ttyUSB0 --chipname=bl616 \
  --baudrate=115200 --flash --read \
  --start=0xE000 --len=0x2000 --file=/tmp/m62-pt-region.bin
```

This reads the two reference table sectors and does not write flash. To read
the Boot2 region, the same ROM-loader read mode was used:

```sh
/mnt/d/Workspace/bouffalo_sdk/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu \
  --interface=uart --port=/dev/ttyUSB0 --chipname=bl616 \
  --baudrate=115200 --flash --read \
  --start=0x00000000 --len=0x0000E000 --file=/tmp/m62-boot2.bin
sha256sum /tmp/m62-boot2.bin
```

The readback's first 52,576 bytes match the SDK's
`boot2_bl616_isp_release_v8.1.8.bin`; the remaining bytes through `0xE000` are
`0xFF`. This identifies the Boot2 image but does not validate application
startup on the M62. The loader logged that it used a default flash
configuration; do not infer that this is safe for programming.

At the initial ROM readback, FW slot 0 was active at `0x10000`. The app-only
target uses `flash_prog_cfg_m62_app.ini`: it erases only sectors covered by
the app image (`erase = 1`), rejects images larger than `0x200000` bytes, and
has no Boot2, partition-table, or data-partition image. It preserves Boot2,
both table copies, the FW slot 1/`mfg` range, and media/PSM/KEY/DATA/factory.
It resets the CPU after programming but does not read or update the device
table. Latest post-XZ-OTA startup logs show slot 0 active, superseding the older
slot-1 assumption. This fixed-address target is a deliberate ROM programming
operation, not a slot-aware updater.

The SDK table also defines `mfg` at `0x210000`, exactly overlapping FW slot
1. The M62 profile enables the SDK A/B OTA backend at the user's request.
OTA has written into the overlapping range; the owner accepted loss of the
aliased manufacturing image. Latest XZ-update startup logs show slot 0 active;
do not infer the final running slot solely from the OTA writer's slot switch.
OTA does not
write Boot2 (`0x00000–0x0DFFF`). The app-only UART target is separate from OTA
and still writes only hard-coded slot 0 without switching boot metadata.

Generic SDK `flash` remains blocked. To build and program only the M62
application slot:

```sh
make -C firmware BOARD=m62_12f flash-app-m62 \
  COMX=/dev/ttyUSB0 BAUDRATE=115200 \
  CROSS_COMPILE="$TOOLCHAIN_BIN/riscv64-unknown-elf-"
```

Do not run the target until you can reliably re-enter ROM download mode for
recovery. The USB-UART workflow is:

1. Connect the kit's USB connector with a data-capable cable and install the
   CH340 driver if needed. Identify its serial port in Device Manager
   (Windows); on this board the Linux device is `/dev/ttyUSB0`.
2. Verify that the device is in ROM download mode. The official Zephyr board
   instructions document pressing RST to run an application, but do not
   specify how to enter ROM download mode; the exact PROG/RST sequence still
   needs to be established. Do not guess button timing.
3. Run the M62-only command above. Generic `flash` remains blocked.
4. Reset the kit and capture UART0 logs at 115200 baud to validate startup.
   If it does not boot, re-enter ROM download mode and restore a full-flash
   backup created before programming; this session did not create one.

## Update a running firmware over native USB 2.0

The M62 kit's USB connector is only a CH340 USB-to-UART bridge. For native USB
OTA, connect the SoC's USB_DP/USB_DM on J3 pins 10/9 to a USB 2.0 host with a
properly wired data connection and common ground. Keep the board powered using
the kit's supported power arrangement; do not connect competing 5 V supplies.
The host sees a CDC-ACM serial port. The SDK reference build starts the OTA
service; install `pyserial`, build that image, then upload its SDK-generated
XZ OTA package:

```sh
python3 -m pip install pyserial
python3 tools/usb_ota.py \
  --port /dev/ttyACM0 \
  --image firmware/build/build_out/uni_usb_dongle_bl616.xz.ota
```

On Windows, use the CDC-ACM COM port, for example `--port COM5`. CDC does not
use a UART baud rate. The reference and M62 builds verify the OTA image SHA-256 before switching
the inactive A/B slot and rebooting. This hash detects corruption but is not
a digital signature; only send firmware from a trusted host. The updater
cannot install the first firmware image. On M62, OTA does not affect Boot2,
but its inactive/staging destination can alias `mfg`; the owner accepted that
loss and the latest update completed successfully. USB CDC enumeration, OTA, reboot,
and status/probe responses were verified on the connected kit. WSL USB/IP
reports 480 Mbps for the device connected through the external J3 wiring.

If an upload times out waiting for BEGIN, test the CDC protocol without
starting an update or touching flash:

```sh
python3 tools/usb_ota.py --port /dev/ttyACM0 --probe
```

A successful probe confirms device-side framing and response transport, but
does not validate flash writes or the OTA package. If it times out too, check
that the device is running the latest OTA-enabled M62 build, verify the
device-state response, and capture UART0 logs while probing. The firmware is
configured to chase red/green/blue after accepting OTA BEGIN, but visual LED
behavior has not been verified on the physical kit.

## References

The reference projects informed feature requirements only; their source code
is not vendored:

- [DS5Dongle BL618](https://github.com/sqlCRT/ds5dongle-bl618-opensource)
- [USBPods Pico2W](https://github.com/wasdwasd0105/USBPods-Pico2W)
