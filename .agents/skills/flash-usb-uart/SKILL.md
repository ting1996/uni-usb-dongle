---
name: flash-usb-uart
description: >-
  Flash built BL616 M62-12F-Kit firmware over USB-UART (CH340 COM port via
  tools/windows.ps1 or BLFlashCommand). Runs pre-flight checks and dry-run
  validation, then MUST stop and ask the user to press the Boot button to enter
  ROM download mode and approve before flashing, resuming the current task
  after approval.
---

# Flash Firmware via USB-UART (BL616 / M62-12F-Kit)

Use this skill whenever the user asks to flash, recover, or program the M62-12F-Kit over the CH340 USB-to-UART port.

## Safety Rules

- **Only target `BOARD=m62_12f`**. Never flash `BOARD=bl616dk` or run the generic SDK `flash` target.
- **App-only slot-0 scope**: UART flashing uses `firmware/flash_prog_cfg_m62_app.ini` (`erase = 1`, `boot2_isp_mode = 0`, `address = 0x00010000`, max `2097152` bytes / `0x200000`). It preserves Boot2 (`0x00000–0x0DFFF`), partition tables (`0x0E000–0x0FFFF`), and `PSM`/data partitions, and writes fixed slot 0 without modifying boot metadata.
- **Never write flash without completing the Mandatory Stop Gate and receiving explicit user approval.**

## Step 1: Pre-Flight Validation (Read-Only)

1. Confirm the firmware image exists at `firmware\build\build_out\uni_usb_dongle_bl616.bin`.
   - If missing or stale after source edits, build it first:
     ```powershell
     .\tools\windows.ps1 build -Target Firmware -Board m62_12f
     ```
2. Detect the CH340 USB-UART port (`VID:PID 1A86:7523`, typically `COM7` on Windows or `/dev/ttyUSB0` on Linux/WSL):
   ```powershell
   .\tools\windows.ps1 doctor
   ```
   - Ensure no UART logger (`tools/uart_monitor.py`) or terminal currently holds `<COMx>` open.
   - Note: On Windows, COM port checks and `BLFlashCommand.exe` may require running outside the terminal sandbox.
3. Run the mandatory dry-run validation (opens no device and writes no flash):
   ```powershell
   .\tools\windows.ps1 flash -Method uart -Board m62_12f -Device <COMx> -Firmware firmware\build\build_out\uni_usb_dongle_bl616.bin -DryRun
   ```
   - Confirm the output prints image size (`<= 2097152` bytes), SHA-256, `Method : uart`, and valid `firmware/flash_prog_cfg_m62_app.ini` safety settings (`erase = 1`, `boot2_isp_mode = 0`, `address = 0x00010000`).

## Step 2: MANDATORY STOP GATE — Ask User to Press Boot Button & Approve

**STOP IMMEDIATELY before running `-ConfirmFlash`, `BLFlashCommand.exe`, or `make flash-app-m62`.**

Present the pre-flight summary to the user:
- Board: `m62_12f` (`bl616`)
- UART Port & Baud: `<COMx>` at `115200` baud
- Firmware File, Byte Size, and SHA-256
- Flash Boundary: App-only slot 0 at `0x00010000` (Boot2 and partition tables preserved)

Then ask the user to prepare the hardware and reply with approval:
1. Ensure the M62-12F-Kit USB-UART (CH340) cable is connected.
2. Put the board into BL616 UART ROM download mode (**press/hold the Boot button** while resetting/power-cycling the kit as required by the board).
3. *(Optional)* Ask whether they want a read-only flash backup first (`.\tools\windows.ps1 backup -Device <COMx> -Region table|boot2|full`).
4. Ask them to confirm once the board is in Boot mode and ready to flash.

**End your turn and wait for the user's approval.** Do not run any flash-writing command in this turn.

## Step 3: Execute USB-UART Flash (Only After User Approves)

Once the user confirms the board is in Boot mode and approves flashing:

- **Windows (PowerShell)**:
  ```powershell
  .\tools\windows.ps1 flash -Method uart -Board m62_12f -Device <COMx> -Firmware firmware\build\build_out\uni_usb_dongle_bl616.bin -ConfirmFlash
  ```
- **Linux / WSL**:
  ```sh
  make -C firmware BOARD=m62_12f flash-app-m62 \
    COMX=/dev/ttyUSB0 BAUDRATE=115200 \
    CROSS_COMPILE="$TOOLCHAIN_BIN/riscv64-unknown-elf-"
  ```

If the ROM handshake fails or times out:
- **Do not retry blindly in a loop.**
- Report the exact command output and ask the user to re-enter Boot mode (press Boot button + reset) and re-approve before trying again.

## Step 4: Post-Flash Verification & Continue Current Task

1. Verify boot / USB enumeration as appropriate for the task:
   - UART boot logs (uses `Serial(port=None)` with `dtr=False, rts=False`):
     ```powershell
     python tools/uart_monitor.py --port <COMx> --duration 10
     ```
     *(Start UART monitor before opening HID/audio clients if running both, because opening CH340 can briefly reset the board.)*
   - Management HID check (when native USB is attached to host):
     ```powershell
     python tools/usb_hid_check.py --device auto
     ```
2. Record the flash outcome, port, and SHA-256, then **continue the remaining steps of the current task** (such as system testing or GitHub push).
