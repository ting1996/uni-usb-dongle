# Windows 10/11 Native Build, Test, and Flash Guide

This document describes how to build the host-testable core and M62 firmware,
run automated and Windows kernel hardware tests, and validate or flash firmware
natively on Windows 10/11 without WSL using PowerShell (`tools\windows.ps1`).
Existing Linux workflows remain intact.

## Prerequisites and Software

| Component | Purpose | Requirement / Notes |
| --- | --- | --- |
| **PowerShell** | Unified entry point | Windows PowerShell 5.1 (built-in) or PowerShell 7+ (`pwsh`) |
| **C11 Build Tools** | Host C library & unit tests | Visual Studio 2022/2026 ("Desktop development with C++", CMake 3.20+, Ninja, MSVC `cl.exe`) or standalone CMake + Ninja + GCC/Clang |
| **Python 3.8+** | Test suite, kernel HID/audio checks, serial tools | Standard CPython (e.g. Python 3.15); install `pyserial` (`python -m pip install pyserial`) if using COM port tools |
| **Node.js 20+** | Browser WebHID/protocol tests | Standard `node.exe` on `PATH` (or fallback to VS Code / Antigravity Electron in `ELECTRON_RUN_AS_NODE=1` mode) |
| **Bouffalo SDK** | Firmware builds & UART flashing | Checked out at `..\bouffalo_sdk` or `$env:BL_SDK_BASE`; includes `BLFlashCommand.exe`, `bflb_fw_post_proc.exe`, and GNU Make |
| **Xuantie RISC-V GCC (MinGW)** | Native Windows firmware build | Windows PE toolchain (`D:\Workspace\Xuantie-900-gcc-elf-newlib-mingw-V3.2.0\bin` or `$env:TOOLCHAIN_BIN`) |
| **CH340 Driver** | USB-UART console & ROM recovery | QinHeng CH340 USB-to-UART bridge driver (`VID:PID 1A86:7523`) for the kit's built-in USB connector |

### Environment Variables (Optional Overrides)

`tools\windows.ps1` auto-discovers standard installations and sibling
directories (`..\bouffalo_sdk`, `..\Xuantie-*-mingw-*\bin`), and automatically
skips uninstalled Microsoft Store `python.exe`/`py.exe` redirector stubs. You
can override discovery with:

```powershell
$env:PYTHON_EXE    = "C:\Users\Nghiep\AppData\Local\Python\bin\python.exe"
$env:PYSERIAL_PATH = "C:\Path\To\site-packages"   # optional extra site-packages
$env:NODE_EXE      = "C:\Program Files\nodejs\node.exe"
$env:BL_SDK_BASE   = "D:\Workspace\bouffalo_sdk"
$env:TOOLCHAIN_BIN = "D:\Workspace\Xuantie-900-gcc-elf-newlib-mingw-V3.2.0\bin"
```

## Windows Driver Detection & Why `usbipd list` Shows `COM8`

You do **not** need to change the dongle's default `VID_FFFF&PID_FFFF` for
Windows driver detection on the audio + output-profile HID firmware:

- `MI_00` / `MI_01` / `MI_02`: Windows automatically loads `usbaudio.sys` (**USB Audio
  Device** / `Bluetooth Gamepads + Stereo Audio`, exposing both a 44.1 kHz stereo
  `waveOut` speaker on `MI_01` and a 16 kHz mono `waveIn` microphone with Mute/Volume
  on `MI_02`). *(Note: Bouffalo SDK `btprofile` provides HFP-HF client only, not
  HFP Audio Gateway, so the USB mic streams clean silence from its `mic_pcm` ring
  buffer unless fed by a local/BT voice source.)*
- `MI_03`: Windows automatically loads `HidUsb.sys` (**HID-compliant
  vendor-defined device**, `UsagePage=0xFF00, Usage=0x0001`, 64-byte feature
  reports for management, profile switching, audio metrics, and OTA).
- `MI_04` and `MI_05` (in default `Generic` profile `ffff:ffff`): Windows
  automatically loads `HidUsb.sys` (**HID-compliant game controller**,
  `UsagePage=0x0001, Usage=0x0005`, 10-byte input reports).

When switched (session-only) to `XInput` (`045e:028e`), `PS4` (`054c:09cc`),
`Switch` (`057e:2009`), or `DualSense` (`054c:0ce6`), the dongle re-enumerates keeping UAC1 speaker+mic audio (`MI_00`–`MI_02`)
and the vendor management/OTA HID interface (`MI_03`), while `MI_04` and `MI_05`
expose the two chosen controller interfaces. Python kernel tools detect all five
VID/PIDs and accept `(MI_04, MI_05)`, `(MI_03, MI_04)`, and legacy `(MI_02&Col01, MI_03)` layouts.

If `usbipd list` still labels `1-4 ffff:ffff` as `USB Serial Device (COM8)`,
that comes from a stale `PortName = COM8` registry value cached under
`HKLM\SYSTEM\CurrentControlSet\Enum\USB\VID_FFFF&PID_FFFF&MI_00\...\Device Parameters`
from when the dongle previously ran the legacy CDC-ACM firmware with the same
VID/PID. The current firmware exposes audio and HID interfaces rather than a
CDC serial port; Windows kernel tools and WebHID talk directly to `MI_02` and
`waveOut`.

## Quick Start (`.\tools\windows.ps1`)

### 1. Environment Check (`doctor`)

```powershell
.\tools\windows.ps1 doctor
```

Inspects MSVC / CMake / CTest / Ninja, Python, `pyserial`, Node.js, Bouffalo
SDK tools, the Windows PE Xuantie RISC-V toolchain, built firmware artifacts
with SHA-256 hashes, Windows kernel HID & `waveOut` dongle interfaces, and
connected COM ports.

### 2. Build (`build`)

Build the host C library (`dongle_core.lib`) and 8 host C tests into
`build\windows\`, and/or cross-compile the M62 firmware natively on Windows into
`firmware\build\`:

```powershell
# Host C library & unit tests (Debug by default, or Release)
.\tools\windows.ps1 build
.\tools\windows.ps1 build -Config Release

# Native Windows firmware build (BOARD=m62_12f)
.\tools\windows.ps1 build -Target Firmware -Board m62_12f

# Build both host tests and M62 firmware natively on Windows
.\tools\windows.ps1 build -Target All -Config Release
```

`CMakeLists.txt` enables `CMP0141` (`Embedded` `/Z7` debug information for MSVC
to avoid `mspdbsrv.exe` hangs) and `/experimental:c11atomics` so C11
`<stdatomic.h>` compiles cleanly under MSVC. `Invoke-FirmwareBuild` strips any
directory containing `sh.exe` from the build `PATH` (required by CMake `MinGW
Makefiles`) and cleans stale non-Windows (`/mnt/`) firmware CMake caches if
present.

`BOARD=bl616dk` remains a reference smoke configuration and must not be flashed
to the M62-12F-Kit.

### 3. Automated Host Tests and Windows Kernel Hardware Tests (`test`)

Run the full automated host suite (8 CTest targets, 19 Python `unittest` cases,
and 13 Web/Node.js tests — 40 tests total, requiring no physical hardware):

```powershell
.\tools\windows.ps1 test
```

Run a single suite or filter tests by name/substring:

```powershell
.\tools\windows.ps1 test -Suite C
.\tools\windows.ps1 test -Suite Python
.\tools\windows.ps1 test -Suite Web

# Filter by CTest target, Python module/method, or JS file/test title
.\tools\windows.ps1 test -Name dualsense
.\tools\windows.ps1 test -Name test_usb_ota
.\tools\windows.ps1 test -Name test_web_hid
```

#### Machine-Readable Test Reports

Each `test` invocation writes combined and per-suite reports under
`build\windows\test-results\` (override with `-ReportDir`):

- `test-report.json`: structured summary, per-test status, execution duration,
  and failure details.
- `test-report.xml`: combined UTF-8 JUnit XML report across all executed suites.
- `junit-ctest.xml`, `junit-python.xml`, `junit-web.xml`: raw suite XML files.
- `test-output.log`: captured console stdout/stderr log.

#### Windows Kernel Hardware Tests

To run all non-destructive native Windows kernel checks against the attached
M62 dongle (writes no pairing records and no flash):

```powershell
.\tools\windows.ps1 test -Suite Hardware
```

Or run the individual Windows kernel scripts in `tools/` directly:

```powershell
# Management HID feature-report check, state query, OTA probe, profile switching, and DualSense passthrough
python tools/usb_hid_check.py --device auto
python tools/usb_hid_check.py --device auto --get-profile
python tools/usb_hid_check.py --device auto --set-profile ps4
python tools/usb_hid_check.py --device auto --set-profile dualsense
python tools/usb_hid_check.py --device auto --dualsense-metrics 0 --dualsense-passthrough-test

# Both HID gamepads (MI_04/MI_05 or earlier MI_03/MI_04 or legacy MI_02&Col01/MI_03) via Windows hid.dll + 1000 Hz RTOS clock
python tools/usb_gamepad_check.py --hid auto --cycles 2

# USB audio speaker playback (waveOut) and 16 kHz mono mic capture (waveIn) via Windows winmm.dll
python tools/usb_audio_check.py --device auto --seconds 3 --tone --amplitude 8000
python tools/usb_audio_check.py --device auto --seconds 2 --mic
python tools/usb_audio_check.py --device auto --seconds 3 --inspect-capture

# Bluetooth radio state & paired controller status over Windows HID
python tools/bluetooth_check.py --device auto
python tools/bluetooth_check.py --device auto --scan
```

If testing older CDC-ACM firmware that exposes a native USB COM port instead of
HID/Audio, pass `-Device COMx` (e.g. `.\tools\windows.ps1 test -Suite Hardware -Device COM5`)
to also run `usb_ota.py --probe` and `usb_device_check.py`.

### 4. Flashing Firmware (`flash`)

`.\tools\windows.ps1 flash` enforces pre-flight validation and **never writes to
a physical device without `-ConfirmFlash`**.

Three flashing paths are supported on Windows for `BOARD=m62_12f`:

1. **Windows Kernel HID OTA (`-Method hid`, default for audio+HID firmware)**:
   - Uses `tools/usb_hid_check.py` over Windows `hid.dll` (`MI_02`, or legacy
     `MI_02&Col02`) with an SDK `.xz.ota` package (default:
     `firmware\build\build_out\uni_usb_dongle_bl616.xz.ota`).
   - Preserves Boot2 (`0x00000–0x0DFFF`), but the SDK inactive slot aliases
     `mfg` at `0x210000` (see [hardware findings](hardware.md)).
2. **Legacy Native USB CDC-ACM OTA (`-Method ota`)**:
   - Uses `tools/usb_ota.py` with an `.xz.ota` package on a CDC COM port
     (`-Device COMx`).
3. **App-only USB-UART Slot-0 Programming (`-Method uart`)**:
   - Uses `BLFlashCommand.exe` with `firmware\flash_prog_cfg_m62_app.ini`
     (`erase = 1`, `boot2_isp_mode = 0`, `address = 0x00010000`) and a `.bin`
     image (default: `firmware\build\build_out\uni_usb_dongle_bl616.bin`).
   - Enforces the 2,097,152-byte (`0x200000`) slot-0 size bound and requires
     BL616 ROM download mode on the CH340 UART port (`COM7`).

#### Dry-Run, Probe, and Authorized Flash Examples

```powershell
# 1. Validate image, flash boundaries, and exact command without opening hardware
.\tools\windows.ps1 flash -Method hid -DryRun
.\tools\windows.ps1 flash -Device COM7 -Firmware firmware\build\build_out\uni_usb_dongle_bl616.bin -DryRun

# 2. Non-writing protocol probe (no flash writes)
.\tools\windows.ps1 flash -Method hid -Probe
.\tools\windows.ps1 flash -Device COM5 -Method ota -Probe

# 3. Explicitly authorized Windows HID OTA flash
.\tools\windows.ps1 flash -Method hid -Firmware firmware\build\build_out\uni_usb_dongle_bl616.xz.ota -ConfirmFlash

# 4. Explicitly authorized app-only UART slot-0 flash (device must be in ROM download mode)
.\tools\windows.ps1 flash -Device COM7 -Firmware firmware\build\build_out\uni_usb_dongle_bl616.bin -ConfirmFlash
```

### 5. Read-Only UART Flash Backup & Partition Verification (`backup`)

Reads flash regions strictly via `BLFlashCommand.exe --flash --read` (never
erases or writes), refuses to overwrite existing output files unless `-Force` is
passed, and automatically inspects/CRC-verifies `BFPT` dual-copy partition
tables on `table` and `full` dumps using [tools/bl616_pt_check.py](../tools/bl616_pt_check.py).
Requires the M62 to be in BL616 ROM download mode on `COM7`:

```powershell
# Dry-run validation (computes command, checks BLFlashCommand.exe, opens no device)
.\tools\windows.ps1 backup -Device COM7 -Region table -Output build\windows\pt-dryrun.bin -DryRun
.\tools\windows.ps1 backup -Device COM7 -Region full -Output build\windows\flash4m-dryrun.bin -DryRun

# Real read-only flash backup in ROM download mode (defaults output to backups\m62-<region>-<timestamp>.bin)
.\tools\windows.ps1 backup -Device COM7 -Region table
.\tools\windows.ps1 backup -Device COM7 -Region boot2
.\tools\windows.ps1 backup -Device COM7 -Region full

# Inspect or JSON-export an existing partition dump directly
python tools/bl616_pt_check.py backups\m62-table.bin --offset 0xE000 --json
```

### 6. Clean (`clean`)

Removes only the Windows host build directory (`build\windows\`), leaving
historical bench logs in `build\*.log` and firmware artifacts in
`firmware\build\build_out\` intact:

```powershell
.\tools\windows.ps1 clean
```

## Troubleshooting

- **`python.exe` / `py.exe` fails with "The file cannot be accessed by the system"**:
  Windows 10/11 creates zero-byte Microsoft Store App Execution Aliases in
  `%LOCALAPPDATA%\Microsoft\WindowsApps`. Install CPython or set
  `$env:PYTHON_EXE`; `windows.ps1` automatically skips broken Store stubs.
- **`Illegal System DLL Relocation` or `Access is denied` inside restricted sandboxes**:
  32-bit MinGW binaries built with `IMAGE_FILE_RELOCS_STRIPPED` (such as
  `Xuantie-900-gcc-elf-newlib-mingw-V3.2.0`), user-local Python managers, and raw
  `\\?\hid#...` kernel handles require running in a normal (unsandboxed) Windows
  PowerShell session.
- **If the dongle is attached to WSL2 via `usbipd`**:
  When attached to WSL2, Windows cannot open the dongle's HID/Audio devices.
  Run `usbipd list` and `usbipd detach --busid <bus-id>` from an elevated
  PowerShell to return it to Windows.
- **Opening the CH340 UART port resets the dongle**:
  Use `tools/uart_monitor.py` (which sets `dtr = False` and `rts = False`
  before calling `open()`), and start monitoring before opening HID/audio
  clients.

## Known Limitations

- **Authorized live flashing**: Dry-run validation (`-DryRun`) and non-writing
  probes (`-Probe`) are verified, whereas actual flash writes (`-ConfirmFlash`)
  over Windows HID, CDC, and `BLFlashCommand.exe` UART are intentionally gated
  on explicit user authorization.
- **Audible headphone verification**: `usb_audio_check.py` verifies Windows
  `waveOut` PCM playback and dongle USB PCM reception counters; audible playback
  still requires an authenticated Bluetooth headphone and listener confirmation.
