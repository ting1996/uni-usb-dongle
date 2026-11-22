# WebHID configuration and OTA

The firmware and browser now support WebHID for status, saved controller
selection/removal, and firmware updates. Web Serial remains a fallback.
Bluetooth pairing and audio backends are still unimplemented; WebHID does not
enable those capabilities by itself.

## Current verification

- Built and installed the M62 WebHID image through the existing CDC updater.
- Verified real feature-report status, unsupported radio commands, empty-slot
  rejection, OTA probing, and an 18-fragment maximum-size frame through hidraw.
- Completed a **69,088-byte OTA upload through HID feature reports**, then
  verified reboot, HID status/probing, CDC, both gamepads, simultaneous inputs,
  and the corrected 1000 Hz timer.
- Verified transport isolation during a live HID update: CDC status remains
  readable, while CDC abort and selection commands return busy; HID OTA then
  completes normally.
- Passed 7 native C tests, 16 Python tests, and 10 JavaScript tests. Browser UI
  connect/status/capability gating and OTA completion/disconnect were exercised
  in Chromium using a simulated HID device.
- Native Windows browser-to-device access is not yet verified: the native USB
  device is currently attached to WSL through USB/IP, rather than available to
  Windows HID. Do not confuse the simulated browser test with a hardware test.

The installed XZ OTA package SHA-256 is
`c3f129763adfb0c779f05ea4fb983b27b78ac5c8757f26f279c42e42b1b4d57e`.
UART startup identifies build `22:07:27, Nov 15 2026` and FW slot 0 active
after the final HID update (partition-table age 27, FW entry age 16).

## Connect from the browser

You can either use the hosted GitHub Pages deployment at
`https://ting1996.github.io/uni-usb-dongle/` (once GitHub Pages is enabled in
repository settings) or serve the panel locally:

1. If running locally, serve the panel from the project root:

   ```sh
   python3 -m http.server 8000 --bind 127.0.0.1 --directory web
   ```

2. Open `https://ting1996.github.io/uni-usb-dongle/` or `http://localhost:8000`
   in desktop Chrome or Edge. WebHID requires a secure context such as localhost
   or HTTPS, plus a user gesture and device selection. Browser support and
   feature APIs are described in the
   [Chrome WebHID documentation](https://developer.chrome.com/docs/capabilities/hid).
3. On Windows, ensure the **native USB** device is available to Windows HID.
   If it is forwarded to WSL, use `usbipd list` in PowerShell to identify the
   native `ffff:ffff` device and `usbipd detach --busid <native-bus-id>` to
   release that device. Keep the separate CH340 UART bridge attached to WSL
   if UART logging is needed. Do not detach an unrelated USB device.
4. Leave WebHID selected, click Connect, and select the dongle. The browser
   filters on usage page `0xff00`, usage `1` across all five output-profile
   VID/PIDs (`ffff:ffff` Generic, `045e:028e` Xbox 360 XInput, `054c:09cc`
   PS4 DualShock 4, `057e:2009` Switch Pro, `054c:0ce6` PS5 DualSense), so only the management collection
   is eligible regardless of the active profile.
5. Inspect saved controller slots, discovery/pairing controls, session-only
   USB output profile selection (`0x1e` / `0x1f`, resets to Generic on reboot),
   and live audio/AVRCP/PCM metrics (`0x18` / `0x20`; older firmware missing
   `0x1e`/`0x20` gracefully disables profile switching). Disconnect allows
   switching to legacy USB serial.
6. For OTA, select the SDK-generated `*.xz.ota` package and acknowledge the
   existing `mfg` overlap. Controls are locked during transfer. Successful
   completion closes the browser connection; reconnect after the device boots.

When native USB is released to Windows, its WSL ttyACM/hidraw nodes disappear;
Linux tools cannot access it until it is attached to WSL again.

## GitHub Pages deployment

The workflow in [`.github/workflows/deploy-pages.yml`](../.github/workflows/deploy-pages.yml)
runs the WebHID Node.js tests, stages a static bundle via
`node tools/build_web_pages.mjs --out _site` (converting `.mjs` ES modules to
`.js` with updated imports and adding `.nojekyll`), and deploys to GitHub Pages
at `https://ting1996.github.io/uni-usb-dongle/`.

To enable hosting on GitHub:
1. Open repository **Settings → Pages** and set **Build and deployment → Source**
   to **GitHub Actions**.
2. Trigger **Deploy WebHID to GitHub Pages** in the **Actions** tab (or push to
   `main` touching `web/`).

## Verify through Linux hidraw

Identify the management node by its USB ancestry and report descriptor. On
the current kit it is `/dev/hidraw0`; the second gamepad lacks the vendor
collection. Node numbers may change after reconnecting.

```sh
python3 tools/usb_hid_check.py --device /dev/hidraw0
```

This is a non-writing check. Raw HID needs read/write permission. On this
WSL installation the verified test was run with WSL's normal root option,
without changing device permissions:

```sh
wsl.exe -d Ubuntu -u root -- python3 \
  /mnt/d/Workspace/uni-usb-dongle/tools/usb_hid_check.py --device /dev/hidraw0
```

Only an explicit `--image <package>` performs OTA. Keep the same partition,
power, and recovery considerations as the [device runbook](device-validation.md).
Close competing applications before a firmware update.

## Protocol and firmware boundaries

- In the current audio + output-profile firmware, Interface 3 is dedicated to
  vendor management/OTA HID with a 23-byte report descriptor (Feature Report
  ID 2 via EP0, status interrupt IN `0x85`), following the 3-interface UAC1
  speaker + microphone collection (`MI_00`..`MI_02`). In the default `Generic` profile,
  Interfaces 4 and 5 each expose a 77-byte gamepad report descriptor with
  input report ID 1. When switched to `XInput`, `PS4`, `Switch`, or `DualSense`, Interfaces
  4 and 5 re-enumerate with that profile's controller descriptors while
  Interface 3 remains management HID. (Host tools retain detection compatibility
  for earlier `MI_02`/`MI_03`/`MI_04` layouts.)
- Gamepad input is ten bytes on USB: ID 1 followed by the existing nine-byte
  normalized report. The routing sink still consumes nine bytes; controller
  adapters do not insert the ID themselves.
- Feature reports are 64 bytes on USB: ID 2, then a 63-byte payload containing
  a little-endian frame offset (2 bytes), fragment length (1), and up to 60
  bytes of the existing checksummed `UOTA` frame. Padding bytes are zero.
- Requests may contain the full 1024-byte protocol payload. A new offset-zero
  fragment replaces an incomplete request and clears the prior reply. Only
  one complete request is dispatched at a time; out-of-order/oversized or
  corrupt frames never reach the command handlers.
- Replies fit one fragment (maximum 60 framed bytes, 44 payload bytes).
  Current replies use at most 27 payload bytes. An empty feature reply means
  pending; a completed reply stays readable until a new request starts.
- The browser queues commands and matches response command/sequence numbers.
  Timeouts close the logical transport so late native operations cannot mix
  with a subsequent request. Reconnect to recover.
- Feature callbacks only copy bounded data. The existing FreeRTOS protocol
  task handles settings and OTA work. Successful HID FINISH waits for the
  reply to be read, up to three seconds, then delays 100 ms before resetting.
- An active OTA session belongs to CDC or HID. Other transports may read
  status/diagnostics, but cannot mutate state or take over the update.

No extra interrupt endpoint was allocated. The inherited CDC notification
endpoint limitation described in the [bench report](real-device-results.md)
remains; adding audio still requires a deliberate USB FIFO/interface design.
