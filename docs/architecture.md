# Project architecture and implementation gates

The first scaffold keeps device-management rules portable so they can be
tested without hardware. The current [device catalog](../include/dongle/device_catalog.h)
tracks two paired controllers and maps each fixed slot to a distinct USB
gamepad index. The [settings codec](../src/core/settings_codec.c) stores that
catalog in a versioned, checksummed record. A Bouffalo SDK persistence adapter
stores it atomically in the `PSM` LittleFS partition and refuses to format a
non-erased partition it cannot mount.

## OS and stage progress

**FreeRTOS is the firmware's main OS.** The firmware entry point follows the
Bouffalo SDK's BL616 FreeRTOS application model and uses the SDK logger. The
host-testable core remains independent of the RTOS.

- **Stage 1 — hardware/platform verification:** SoC/kit identity, USB
  connector-vs-header wiring, UART0 pin mapping, and a 4 MiB flash model have
  been confirmed from the vendor schematic, board metadata, and a read-only
  UART ROM-loader session. The session identified flash JEDEC `C8 60 16`,
  4 MiB capacity, and valid partition-table copies matching the SDK A/B/PSM
  layout. The Boot2 readback matches the SDK BL616 v8.1.8 release image. The
  Zephyr board metadata describes a different software partition map.
  The M62 firmware has since booted after native USB CDC OTA; enumeration and
  OTA were verified, with WSL USB/IP reporting 480 Mbps. A repeatable UART ROM
  recovery sequence still requires bench verification; details are in
  [hardware findings](hardware.md).
- **Stage 2 — target foundation:** FreeRTOS startup, SDK logging, settings
  serialization, and a LittleFS/MTD adapter are scaffolded. The adapter opens
  the named `PSM` partition (no hard-coded flash writes), formats only a
  completely erased partition, and replaces settings via a temp-file rename.
  An M62-specific profile now reuses the SDK BL616 startup and linker setup
  while omitting unverified board GPIO/RF setup, rejecting PSRAM/RF builds,
  and avoiding BL616DK flash timing calibration. A dedicated UART target is
  hard-coded to FW slot 0 at `0x10000`, with a size
  bound and programmed-section erase; it does not include Boot2, either
  partition table, or data partitions. Latest post-XZ-OTA UART logs show slot 0
  active; the target does not inspect or switch boot metadata and must not be
  treated as a slot-aware updater. The native USB CDC-ACM OTA receiver uses the
  SDK A/B writer and has been verified on the M62. OTA leaves Boot2 unchanged,
  but slot 1 aliases `mfg`; the owner accepted loss of the manufacturing
  image for this device. The kit's CH340 USB connector is not used for OTA.
  Repeatable ROM recovery and LED behavior remain unverified.

## Intended boundaries

```text
Browser configuration
        | validated USB transport / update protocol
Device manager -------- Persistent settings and link keys
        |                         |
Controller host (2 links)      Audio router
        |                         |
USB HID gamepads (2)       USB audio <-> Bluetooth headset
        \_________________________/
          validated board/SDK adapters
```

The M62 startup profile uses verified SoC/flash behavior. The latest Stage 3–5
build was installed over native USB OTA; the device returns the 21-byte
device-state response and passes the read-only OTA probe after reboot.
Two USB HID gamepad interfaces now coexist with CDC and have passed real-device
synthetic report, isolation, and automatic-release checks. A measured RTOS timer
misconfiguration was corrected to match the board's 1 MHz MTIME clock.
Bluetooth HID and audio backends remain unavailable. The SDK A/B writer does
not touch Boot2, but the owner accepted loss of the overlapping FW slot
1/`mfg` region for this device; see [hardware findings](hardware.md).

## Stages 3–5 current status

- **Stage 3 — controller routing and USB output profiles:** the portable core
  forwards each controller's normalized nine-byte report (and full bidirectional
  DualSense passthrough: BT `0x31`→USB `0x01` 64B input, BT LED unlock + USB `0x02`→BT `0x31`
  output with `0xA2` CRC-32, bidirectional `0x80`/`0x81`/`0x08`/`0x05`/`0x09`/`0x20` feature
  reports with `0x53`/`0xA3` CRC-32, and `0x21` passthrough telemetry) through
  `dongle_output_encode` (`output_profile.c` / `usb_profiles.c`) to the active
  USB output profile (`Generic` dual gamepads on Interfaces 3–4, `XInput`
  `045e:028e`, `PS4` `054c:09cc`, `Switch` `057e:2009`, or `DualSense` `054c:0ce6`), switched dynamically
  (session-only) over management HID with BL616 shared IN/OUT FIFO gating.
  Bluetooth Classic discovery, pairing, and DualSense report translation are
  implemented in `bluetooth.c` and validated with a paired DualSense.
- **Stage 4 — audio:** bounded single-producer/single-consumer PCM streams
  feed UAC1 44.1 kHz stereo speaker playback (`usb_audio.c`, Adaptive ISO `EP 0x03`, `MI_01`)
  into the SDK A2DP SBC source (`bluetooth.c`), alongside a 16 kHz mono USB microphone (`EP 0x84`,
  `MI_02`, Feature Unit Mute/Volume) that streams clean silence until an HFP-AG/ADC voice source
  feeds `mic_pcm`, now with `CONFIG_BT_AVRCP=y` (AVRCP Target) and live PCM peak telemetry (`0x20`).
  Audible output through the Theater U neckband speaker remains under active bench confirmation.
- **Stage 5 — browser configuration & host tooling:** the WebHID page (with
  legacy Web Serial fallback) exposes controller slots, Bluetooth
  discovery/pairing, session USB output profile selection, audio/AVRCP metrics,
  and XZ OTA across all five profile VID/PIDs on dedicated management Interface
  3 (`UsagePage=0xff00, Usage=1`, Feature Report ID 2). Native Windows CLI
  orchestration in `tools/windows.ps1` covers `doctor`, `build`, `test`,
  `flash`, and read-only UART `backup` with `bl616_pt_check.py`.

## Staged implementation

1. Kit schematic, USB wiring, flash layout (`BFPT` verified), and M62 startup
   profile are established; repeatable PROG/RST ROM-mode timing and full UART
   flash backup execution still need bench capture.
2. M62 startup, LittleFS `PSM` storage, USB XZ OTA, and read-only UART
   backup/partition verification tooling exist; interrupted-update rollback tests
   remain open.
3. Bluetooth Classic HID-host, DualSense adapter, and all five USB output
   profiles (`Generic`, `XInput`, `PS4`, `Switch`, `DualSense`) are wired; per-profile host
   and physical button/stick validation remain to be exercised across platforms.
4. UAC1 capture, A2DP SBC source, AVRCP Target, live PCM metrics, and SBC RTP
   CRC-8 validation are implemented; physical Theater U audible playback
   confirmation remains open.
5. Browser WebHID UI, kernel Python tools, and native Windows `windows.ps1`
   entry point cover status, pairing, profile switching, OTA, and backups.
6. Documented Windows/Linux workflows and automated C/Python/Node/Hardware test
   suites run natively on Windows; live profile-switching and hardware bench
   validation complete the release gate.

No source is vendored from the reference projects. Any future dependency must
be reviewed independently for technical fit and license obligations.
