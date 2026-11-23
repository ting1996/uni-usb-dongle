# Uni USB Dongle

Bluetooth-to-USB bridge for game controllers and audio headsets on a Bouffalo BL616 SoC (Ai-Thinker M62-12F-Kit).

![Version 1.2.0](https://img.shields.io/badge/version-1.2.0-blue)
[![CI](https://github.com/ting1996/uni-usb-dongle/actions/workflows/ci.yml/badge.svg)](https://github.com/ting1996/uni-usb-dongle/actions/workflows/ci.yml)
[![WebHID Control Center](https://img.shields.io/badge/WebHID-Control%20Center-8a2be2)](https://ting1996.github.io/uni-usb-dongle/)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)

## Overview & Key Features

Uni USB Dongle pairs up to two Bluetooth Classic game controllers and one Bluetooth headphone, presents them to the host over native USB 2.0 using a selectable output profile, and provides a 5-tab browser Control Center over WebHID for pairing, audio routing, diagnostics, and firmware updates. All portable protocol, routing, and persistence logic lives in a host-testable C11 core (`include/dongle/`, `src/core/`); BL616 FreeRTOS hardware integration is isolated under `firmware/`.

- **Five USB output profiles** — Runtime-switchable `Generic` (`FFFF:FFFF`), `XInput` (`045E:028E`), `PS4` DualShock 4 (`054C:09CC`), `Switch` Pro (`057E:2009`), and `DualSense` (`054C:0CE6`), with full DualSense native passthrough (rumble, adaptive triggers, and `0x05`/`0x08`/`0x09`/`0x20`/`0x80`/`0x81` feature reports with CRC-32).
- **UAC1 stereo audio & voice microphone** — 44.1 kHz stereo speaker playback via A2DP SBC source and AVRCP Target (`CONFIG_BT_AVRCP=y`), plus a 16 kHz mono USB microphone fed by Bluetooth headphone HFP-AG (`mSBC`/`CVSD`) or DualSense wireless Opus mic (selectable `Auto`, `Headphone`, `Controller 1`, `Controller 2`, or `Off`).
- **5-tab WebHID Control Center & XZ A/B OTA** — Browser panel (`Overview & Profile`, `Devices`, `Audio & Mic`, `Diagnostics`, `Firmware & Log`) matching all five profile VID/PIDs on usage page `0xFF00` usage `1` (plus legacy Web Serial CDC fallback), 6-entry Never Connect MAC blocklist, live `0x20`/`0x21` telemetry, and SHA-256-verified XZ A/B OTA.
- **Portable C11 core & reproducible builds** — Zero FreeRTOS/SDK dependencies in the portable core, BL616 FreeRTOS firmware (`configCPU_CLOCK_HZ=1000000UL`), pinned `third_party/bouffalo_sdk` submodule (Bouffalo SDK v2.3.32), hardware-PWM RGB status LEDs (50% duty: red slot 1, green slot 2, blue headphone; RGB chase during OTA), and native Windows (`tools/windows.ps1`) + Linux tooling.

## Prerequisites & Installation

| Component | Requirement |
| --- | --- |
| Host OS | Windows 10/11 (PowerShell 5.1+, MSVC 2022/2026 or CMake + Ninja) or Linux (CMake 3.20+, C11 compiler) |
| Firmware toolchain | Xuantie RISC-V GCC (`riscv64-unknown-elf-`), Windows PE or x86_64 Linux; add `bin/` to `PATH` |
| Bouffalo SDK | Pinned submodule at `third_party/bouffalo_sdk` (Bouffalo SDK v2.3.32, branch `uni-usb-dongle`) |
| Python 3.8+ | Host unit tests, validators, and OTA scripts; install `pyserial` (`python3 -m pip install pyserial`) for COM/CDC tools |
| Node.js 20+ | WebHID protocol, transport, and Pages bundle tests (`node --test tests/test_web_*.mjs`) |

Clone with the Bouffalo SDK submodule:

```sh
git clone --recurse-submodules https://github.com/ting1996/uni-usb-dongle.git
cd uni-usb-dongle
```

If you already cloned the repository without `--recurse-submodules`:

```sh
git submodule update --init --recursive
```

## Usage / Quickstart

### Windows 10/11 (PowerShell, native)

```powershell
.\tools\windows.ps1 doctor
.\tools\windows.ps1 build -Target All -Config Release
.\tools\windows.ps1 test
.\tools\windows.ps1 test -Suite Hardware
.\tools\windows.ps1 flash -Method hid -DryRun
.\tools\windows.ps1 backup -Device COM7 -Region table
```

### Linux (CMake/Make + Python/Node tests)

```sh
# Host C11 core tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Python unit & repository config tests
python3 -m unittest discover -s tests -p 'test_*.py' -v

# Browser WebHID / protocol tests
node --test tests/test_web_protocol.mjs tests/test_web_hid.mjs tests/test_web_radio.mjs tests/test_web_pages.mjs

# BL616 M62 firmware build (BL_SDK_BASE defaults to third_party/bouffalo_sdk)
export TOOLCHAIN_BIN=/path/to/Xuantie-900-gcc-elf-newlib-x86_64-V3.2.0/bin
export PATH="$TOOLCHAIN_BIN:$PATH"
make -C firmware BOARD=m62_12f \
  CROSS_COMPILE="$TOOLCHAIN_BIN/riscv64-unknown-elf-"
```

### WebHID Control Center

Open the hosted GitHub Pages panel at [https://ting1996.github.io/uni-usb-dongle/](https://ting1996.github.io/uni-usb-dongle/), or serve `web/` locally in desktop Chrome or Edge:

```sh
python3 -m http.server 8000 --directory web
# open http://localhost:8000
```

Choose **WebHID** and click **Connect** to select the dongle management interface (any of the five profile VID/PIDs). Use **USB Serial** only as a fallback for legacy CDC-ACM firmware.

> [!WARNING]
> **M62-12F-Kit hardware safety — read before flashing.**
> - Build and flash only with `BOARD=m62_12f`. **Never** flash `BOARD=bl616dk` or use the generic `flash` target on the kit.
> - `make -C firmware BOARD=m62_12f flash-app-m62` programs **FW slot 0 only** at fixed address `0x10000` (2 MiB / `0x200000` max) without modifying Boot2, partition tables, or boot metadata.
> - Native USB A/B OTA writes the inactive **FW slot 1**, which aliases `mfg` at `0x210000` and **may erase manufacturing data** (Boot2 is untouched).
> - **Native USB 2.0** connects via **J3 pins 10 (D+) and 9 (D-)**; the kit's built-in USB port is a **CH340 UART0** bridge only.
> - Capture a read-only flash **backup** (`.\tools\windows.ps1 backup`) and verify reliable BL616 ROM download mode entry before UART programming.
> - See [docs/hardware.md](docs/hardware.md), [docs/windows.md](docs/windows.md), [docs/webhid.md](docs/webhid.md), and [docs/device-validation.md](docs/device-validation.md).

## Configuration

### Environment variables

| Variable | Purpose |
| --- | --- |
| `BL_SDK_BASE` | Bouffalo SDK root. Defaults to `third_party/bouffalo_sdk` (`tools/windows.ps1` also honors legacy sibling `..\bouffalo_sdk`). |
| `TOOLCHAIN_BIN` | Directory containing `riscv64-unknown-elf-*` (Xuantie RISC-V GCC). |
| `PYTHON_EXE` | Explicit CPython executable for `tools/windows.ps1` (auto-skips broken Store stubs). |
| `PYSERIAL_PATH` | Optional extra `site-packages` path containing `pyserial`. |
| `NODE_EXE` | Explicit `node.exe` path for Web tests in `tools/windows.ps1`. |

### Firmware build flags (`firmware/Makefile`, `firmware/CMakeLists.txt`, `firmware/defconfig`)

| Flag / Option | Default / Values | Purpose |
| --- | --- | --- |
| `BOARD` | `m62_12f` (target) / `bl616dk` (smoke only) | Board profile. Never flash `bl616dk` to the M62-12F-Kit. |
| `CHIP` | `bl616` | Target Bouffalo SoC. |
| `BUILD_DIR` | `build` | CMake build directory; must remain relative to `firmware/` for the SDK linker script. |
| `CROSS_COMPILE` | e.g. `.../riscv64-unknown-elf-` | RISC-V cross-compiler prefix. |
| `COMX` | `COM5` (Windows) / `/dev/ttyUSB0` (Linux) | Serial port for ROM-mode UART flashing (`flash-app-m62`). |
| `BAUDRATE` | `115200` | UART baud rate for `flash-app-m62`. |
| `DONGLE_BT_SIGNAL_TRACE` | `OFF` (CMake option) / `ON` | Enables bounded headphone L2CAP signaling trace during negotiation. |
| `firmware/defconfig` | `CONFIG_FREERTOS=y`, `CONFIG_LITTLEFS=y`, `CONFIG_FOTA=y`, `CONFIG_CHERRYUSB=y`, `CONFIG_BT_BREDR=y`, `CONFIG_BT_A2DP_SOURCE=y`, `CONFIG_BT_AVRCP=y`, `CONFIG_BT_SETTINGS=y`, `CONFIG_EM_SIZE=32` | SDK Kconfig feature profile. |

### Runtime & persisted settings (`PSM` partition)

Stored in the Bouffalo LittleFS/MTD `PSM` partition using the versioned, checksummed settings/prefs codecs:

| Setting | Values |
| --- | --- |
| Output profile & boot default | `Generic`, `XInput`, `PS4`, `Switch`, or `DualSense` (runtime switch + optional boot persistence) |
| Preferred audio codec | `Auto (SBC)` or `SBC 44.1 kHz Stereo` |
| Microphone source | `Auto (Headphone → Controller)`, `Headphone (HFP)`, `Controller 1 (DualSense)`, `Controller 2 (DualSense)`, or `Off` |
| Paired devices | Up to 2 controllers + 1 Bluetooth headphone |
| Never Connect blocklist | Up to 6 blocked Bluetooth MAC addresses |

## Project Structure

```text
.
├── include/dongle/            # Portable C11 core headers (zero FreeRTOS/SDK deps)
├── src/core/                  # Portable C11 core implementations
├── firmware/                  # BL616 FreeRTOS firmware, board configs, Makefile & CMake
├── third_party/bouffalo_sdk/  # Pinned Bouffalo SDK v2.3.32 Git submodule
├── web/                       # 5-tab WebHID Control Center SPA
├── tools/                     # windows.ps1, kernel HID/audio/BT checks, OTA & PT verifier
├── tests/                     # CTest, Python unittest, and Node.js Web test suites
├── docs/                      # Hardware, architecture, Windows, WebHID & validation docs
└── backups/                   # Historical pre-radio firmware snapshots
```

## Roadmap, Contributing & License

### Remaining Bench Milestones

- Document the repeatable physical `PROG`/`RST` button sequence for entering BL616 UART ROM download mode and capture an on-device full/table backup with `.\tools\windows.ps1 backup`.
- Visually verify status LED colors on M62 hardware (slot 1 red, slot 2 green, headphone blue, OTA RGB chase) and exercise physical controller/headset sessions end-to-end.
- Confirm audible A2DP/AVRCP playback on Sony BRAVIA Theatre U and validate `XInput`, `PS4`, `Switch`, and `DualSense` profiles against target hosts.
- Verify interrupted-OTA rollback behavior against dual-slot Boot2 metadata.

### Contributing

- Run all host test suites before opening a pull request: `.\tools\windows.ps1 test` on Windows, or CTest + `python3 -m unittest discover -s tests -p 'test_*.py' -v` + `node --test tests/test_web_*.mjs` on Linux.
- Keep `include/dongle/` and `src/core/` strict C11 with zero FreeRTOS or Bouffalo SDK dependencies, warning-free under MSVC (`/W4`) and GCC/Clang (`-Wall -Wextra -Wpedantic -Werror`).
- Keep firmware protocol, `web/` UI, host `tools/`, and unit tests synchronized in the same change whenever commands, capabilities, or payloads change.

### References

The following projects informed feature requirements only; their source code is not vendored:

- [DS5Dongle BL618](https://github.com/sqlCRT/ds5dongle-bl618-opensource)
- [USBPods Pico2W](https://github.com/wasdwasd0105/USBPods-Pico2W)

### License

This project is licensed under **GPL-3.0-or-later** — see [LICENSE](LICENSE) for the full GNU General Public License v3.0 text. The `third_party/bouffalo_sdk` directory is a separate Git submodule distributed under the **Apache License 2.0**, which is compatible with GPLv3.
