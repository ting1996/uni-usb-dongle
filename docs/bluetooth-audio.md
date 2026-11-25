# Bluetooth controllers and stereo audio

This firmware targets a PS5 DualSense and Sony BRAVIA Theatre U. The BL616
controller library provides two Classic ACL links: one controller plus one
headphone, or two controllers. A third simultaneous Classic device cannot fit.

The default `Generic` USB output profile exposes a UAC1 stereo playback + mono 16 kHz microphone device
(interfaces 0, 1, and 2), HID gamepads starting at interface 3 (`MI_03`, and `MI_04` in
two-controller mode; `IN 0x81/0x82`), and dedicated vendor management/OTA HID after the gamepad
interface(s) (`MI_04` in one-controller mode, `MI_05` in two-controller mode; feature report ID 2
via EP0, status interrupt IN `0x85`). Audio playback uses isochronous `OUT 0x03`, asynchronous
feedback `IN 0x84` (shared EP number/direction per UAC1 descriptor), and USB mic capture uses
`IN 0x84`. Switching to `XInput` (`045e:028e`), `PS4` (`054c:09cc`), `Switch` (`057e:2009`), or
`DualSense` (`054c:0ce6`, `bcdDevice = 0x0100`, Sony USB strings) keeps UAC1 audio and management HID
while switching the gamepad interfaces (`MI_03+`) to the selected profile; in single-controller console
modes, Gamepad 0 uses a dedicated `OUT` FIFO `F1` (`EP 0x02`) permanently armed for rumble and
adaptive trigger output. Output profile and mic source can be persisted across reboots; the previous
pre-radio firmware is retained in [backups/pre-radio](../backups/pre-radio/). UART remains the debug
connection.

## Pairing

1. Open the [configuration page](http://localhost:8000/) in a desktop browser
   supporting WebHID and connect to the dongle.
2. Put the DualSense into pairing mode with Create + PS until its light flashes.
3. Discover, select its Bluetooth address, and choose **Pair controller**.
4. Put the Theater U into pairing mode by holding Power/Bluetooth until its
   pairing announcement. Discover again, select it, and choose **Pair headphone**.
5. Select **Bluetooth Gamepads + Stereo Audio** as the computer's output device.

Saved headphones retry automatically after boot or connection failure. Press PS
to reconnect a saved DualSense. With a headphone ACL active, controller outgoing
pages pause to protect SBC audio; incoming PS-button reconnects remain enabled.
Explicit Disconnect stops retries until a new connect request or reboot.

Pairing is asynchronous. A successful command means the connection request was
queued; refresh the radio and controller status to confirm authentication and
profile connection. The updated UI distinguishes connecting, authenticating,
paired with audio profile pending, audio link connected, streaming, and failure
with its stage and code. Error 111 means the SDK could not start a connection;
it does not establish whether pairing succeeded or the remote refused it.
Pairing keys and controller slots share the existing single
LittleFS mount. SSP Just Works is accepted only for selected or saved peers.

Playback supports 44.1 / 48 / 96 kHz, 16-bit stereo PCM over UAC1 USB Audio, encoded via a custom AVDTP 1.3 / A2DP Source stack supporting SBC, AAC-LC (`vo-aacenc` + RFC 3016 LATM), LDAC (`libldac` up to 96 kHz), aptX, and aptX HD (`libopenaptx`), with automatic fallback and on-the-fly resampling when host and Bluetooth sample rates differ.
Starting in v1.1.0, USB microphone capture (`16 kHz` mono S16_LE) supports both Sony BRAVIA
Theatre U via custom HFP-AG v1.7 (mSBC wideband `16 kHz` with CVSD `8 kHz -> 16 kHz` fallback;
A2DP suspends automatically during SCO mic capture and resumes when closed) and DualSense Bluetooth
embedded Opus mic frames decoded via `libopus`. Controller rumble and adaptive triggers are forwarded
over Bluetooth (`0x31`) with normalized `COMPATIBLE_VIBRATION`/`HAPTICS_SELECT` flags, `-ENOMEM` retry
preservation, and XInput/PS4/Switch rumble translation. Listen at a comfortable volume during tests.

## Connection LEDs

The RGB LED uses hardware PWM at 50% duty for each connected device:

| Color | Device | Connection condition |
| --- | --- | --- |
| Red | Device 1: controller slot 0 | HID interrupt channel connected |
| Green | Device 2: controller slot 1 | HID interrupt channel connected |
| Blue | Device 3: headphone | Bluetooth ACL connected and authenticated |

Disconnected channels are off. Status refreshes every 50 ms. The existing OTA
chase temporarily overrides the connection colors, then restores current status.
White remains the running indicator. A blue LED confirms the headphone Bluetooth
connection; it does not establish that audible playback works. The two-ACL limit
still permits one controller plus a headphone, or two controllers.

GPIO0/1 use PWM CH0 positive/negative outputs, and GPIO30 uses CH3 positive in
differential mode. Both channels use threshold 0..500 in a 1000-count period,
with the peripheral clock divided by 80 (nominal 1 kHz). GPIO muxes gate each
color independently, including the red/blue outputs sharing CH0.

This image was accepted through HID OTA. Real boot readback reported
`channel=0 duty=500/1000 frequency=1000 Hz` and the same values for channel 3.
When Theater U authenticated, UART reported connection mask `04`, enabling blue.
Physical color/brightness confirmation is pending the owner; PWM register
readback does not by itself verify the light emitted by the board.

Sony documents SBC and A2DP support in the [Theatre U specifications](https://helpguide.sony.net/ht/7861/v1/en/contents/TP1001326159.html).
The controller's pairing gesture is described in [PlayStation's guide](https://www.playstation.com/en-gb/support/hardware/pair-dualsense-controller-bluetooth/).

## Radio management protocol

The existing UOTA framing and CRC apply. Addresses use six bytes in display
order (most significant byte first). Replies begin with the existing status byte.
All replies fit one 60-byte HID response fragment, including the 16-byte header.

| Command | Request payload | Successful response |
| --- | --- | --- |
| 0x13 Start discovery | empty | status; results arrive asynchronously |
| 0x14 Connect/pair controller | address[6] | status |
| 0x17 Discovery result | index[1] | status, index, address[6], signed RSSI, name[30] |
| 0x18 Radio status | empty | status, ready, scanning, count, last error, controller mask, streaming, headphone[6], five LE32 counters |
| 0x19 Connect/pair audio | address[6] | status |
| 0x1a Disconnect peer | address[6] | status |
| 0x1c Capture media packet | empty | status (arms capture of next encoded SBC RTP packet) |
| 0x1d Read media packet | empty | status, packet bytes |
| 0x1e Get output profile | empty | 12 bytes: status, version, active, requested, profileMask, codec, streaming, codecMask, transition, failure, savedProfile, flags (`bit0` persisted, `bit1` 2-controller mode) |
| 0x1f Set output profile | profile[1], codec[1], optional flags[1] (`bit0` persist to flash) | 12-byte profile state; schedules USB re-enumeration |
| 0x20 Get audio metrics | empty | 12 bytes: status, flags (`bit0` AVRCP, `bit1` HFP SLC, `bit2` HFP SCO, `bit3` mSBC), avrcpVolume, peakLeft (LE16), peakRight (LE16), nonzeroPackets (LE32), linkRoles |
| 0x21 Get DualSense metrics | slot[1] | 38 bytes (30 in v1.0): slot, connFlags, featMask, counters, lastFeatId, lastHandshake, valid_flag0/1, motors R/L, triggers R2/L2, outPathFlags, fwVerLo |
| 0x22 Get mic config | empty | 16 bytes: status, configuredSource, activeSource, activeCodec, flags, reserved, peak (LE16), decodedFrames (LE32), errorFrames (LE32) |
| 0x23 Set mic source | source[1] (`0` Auto..`4` Off) | 16-byte mic config & live metrics (persisted to flash) |
| 0x24 Remove paired audio | empty | status (clears `dongle-headphone`, unpairs, disconnects A2DP/HFP) |
| 0x25 Get blocklist | empty | status, count (`0..6`), addrLen (`6`), MAC addresses (`6 * count`) |
| 0x26 Block device | address[6] | updated blocklist (also removes/disconnects matching paired peer and saves to flash) |
| 0x27 Unblock device | address[6] | updated blocklist (removes MAC from Never Connect list and saves to flash) |

Counters are USB PCM bytes, PCM bytes submitted to Bluetooth, underflows,
overflows, and audio errors. Submitted bytes do not prove that the headphone
played audible audio; a real listening check is still required.

The extended radio response is 44 bytes (the UI also accepts the earlier
33-byte response). Bytes 33–35 are audio state, error stage and error code;
36–41 contain the target address; 42–43 report ACL connection and authentication.
States are 0 absent, 1 connecting, 2 authenticating, 3 A2DP connecting,
4 A2DP connected, 5 streaming, 6 failed. Error stages are 1 ACL,
2 authentication, 3 A2DP, 4 pairing storage, 5 disconnection.

Linux HID testing uses [bluetooth_check.py](../tools/bluetooth_check.py), e.g.
`python3 tools/bluetooth_check.py --device /dev/hidraw0 --scan` or an explicit
`--pair-controller AA:BB:CC:DD:EE:FF` / `--pair-audio AA:BB:CC:DD:EE:FF`.

## Current silent-playback investigation

The owner reported no audible tone and continued DualSense reconnect failure.
A subsequent UART trace captured actual 608-byte SBC media packets, each with
five 119-byte joint-stereo frames. Their RTP timestamps advanced by wall-clock
milliseconds (for example `0xb1ea` to `0xb200`) instead of 640 audio samples.
The candidate now corrects the packet sample clock and includes a portable
regression test for the captured format, timestamp wrap, and truncated/fragmented
packet rejection. All eight native C tests pass. The correction was accepted through real HID OTA. Five real encoded media
packets now advance by 640 samples each (`[640, 640, 640, 640]` between
packets). A three-second silent transfer received 529176 USB PCM bytes and
submitted 520192 bytes, with no new audio errors or overflows. The owner later
confirmed that the subsequent five-second tone was still inaudible; end-to-end
audible playback remains unresolved despite successful PCM delivery.

The timestamp requirement is specified in the Bluetooth SIG
[A2DP specification, section 4.3.3.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/a2dp_v1-4-1_1752513648/A2DP_v1.4.1/out/en/index-en.html).

The incoming DualSense correction (waiting for controller-created HID channels,
using a 672-byte MTU) was installed through HID OTA. On November 16, after the
owner powered both devices on, the controller reconnected to slot 0 while Theater
U was authenticated. Real controller reports reached USB: the completed-report
counter advanced from 10883 to 11134 over 2009 firmware ticks, and the right-stick
axis changed from 129 to 130. No synthetic reports were injected. Deliberate
button/stick exercise still requires the owner's participation.

Theater U's reconnect exposed another SDK defect: SET_CONFIGURATION with
transaction label 15 was accepted, but the following local OPEN failed with
`Error id`. The SDK wraps its four-bit label to zero, while its OPEN implementation
incorrectly rejects zero. A version-specific wrapper advances an unused zero
label to one before OPEN; final linked disassembly confirms the SDK callback
calls the wrapper. A BR-channel size assertion guards part of this private ABI;
the offset must be rechecked when changing the SDK library. Signaling connection
status now remains profile-connecting until media is ready, and readiness polling
is limited to five times per second.

This correction was built, packaged (XZ contents matched the application), and
accepted through real HID OTA. Theater U automatically reconnected after boot.
A subsequent explicit disconnect/reconnect reached the failing transaction-label
wrap: UART logged `AVDTP OPEN transaction wrap corrected`, followed by streaming
state 5 with no audio error. Thus the workaround was exercised on the actual
headphone rather than inferred from a clean boot. A five-second quiet tone test
received 881868 USB PCM bytes and submitted 875520 bytes to Bluetooth, with zero
new underflows, overflows, or audio errors. The owner confirmed no audible tone.
After the reconnect that exercised the workaround, a second three-second silent
transfer received 529156 USB PCM bytes and submitted 522752 Bluetooth PCM bytes,
again with no new overflow or audio errors.
All eight native C tests and seventeen Python tests pass at that milestone.

### AVRCP Target, live PCM metrics, and SBC validator follow-up

Bench UART logs showed Theater U querying SDP UUID `0x110C` (AVRCP Target) on
connect while `CONFIG_BT_AVRCP` was disabled in `firmware/defconfig` (returning
0 matches and leaving playback/volume signaling inactive). `CONFIG_BT_AVRCP=y`
is now enabled and wired in `bluetooth.c` (AVRCP Target callbacks, playback
status on stream start/suspend/disconnect, passthrough handling, and absolute
volume notifications/commands). To isolate host-side silence from headphone-side
decoding, `0x20` (`GET_AUDIO_METRICS`) tracks live peak left/right PCM sample
magnitudes and non-zero USB audio frame counts, and
[tools/usb_audio_check.py](../tools/usb_audio_check.py) defaults `--amplitude`
to `8000` (~-12 dBFS instead of `1000`) with a pure-Python SBC RTP + CRC-8
validator (`--inspect-capture`).
After the owner pressed PS again, the next real status query confirmed DualSense
connected in slot 0 simultaneously with Theater U streaming (controller mask 1,
audio state 5, authenticated, no audio error). Deliberate button/stick exercise
still needs verification.

The audio checker now permits a quiet tone with an authenticated idle headphone
so USB playback can trigger resume. It fails if an authenticated headphone
receives less than 90% of the expected PCM submissions, including when its stream
was idle before playback. USB reception alone no longer produces a passing
headphone result in that case.

## Validation status

### Intermittent audio with DualSense connected (November 20)

Owner report: Theater U audio drops in and out while the DualSense is connected.
Code inspection found two contributors on the shared radio:

1. **Scatternet.** The SDK's `accept_conn` (`hci_core.c`) accepts every incoming
   BR/EDR ACL as peripheral. The documented PS-button reconnect, which is the
   only reconnect path while the headphone ACL is active, therefore makes the
   dongle a peripheral in the DualSense piconet while it is central for Theater U.
   The controller must follow two unrelated piconet clocks, so SBC media slots
   are lost whenever the controller link is serviced.
2. **Unthrottled controller output.** Every USB output report 0x02 (rumble/LED)
   became an L2CAP interrupt report. Hosts can resend unchanged reports every
   USB frame, and the radio task polls every 2 ms while a controller is active,
   so this could consume airtime and the ACL buffers shared with A2DP.

Changes (pending hardware verification): after each ACL connects, the radio task
issues HCI Switch_Role to become central before starting authentication. If this
fails or times out after 1 s, it retries once after authentication. Once central,
it writes a per-link policy keeping sniff but disabling role switch, so the peer
cannot turn the link back into a scatternet. SDK reference counting is unaffected
(`BT_CONN_BR_INITIAL_MASTER`). Output reports are coalesced: only the latest
pending report is sent, at most one per 8 ms (16 ms while streaming), and an
unchanged report is suppressed unless 1 s has elapsed.

UART shows `ACL connected role=N link=central|peripheral`, then either
`central link role=N policy=0` or `remains peripheral`. To verify: connect
Theater U, press PS to reconnect the DualSense, confirm `central link role=0`,
then play audio for several minutes while using the controller. Compare the radio
status underflow/overflow/error counters with the earlier baseline and get a
listening check. If the DualSense reports `remains peripheral`, the remaining
fallback is Accept_Connection_Request with role central, which requires an SDK
change.

### November 16 follow-up

DualSense pairing and an active controller connection were observed on the real
device. Theater U authenticated and saved its pairing when tested alone, but
audible playback has not been verified. The subsequent connection error exposed
two firmware bugs: a duplicate A2DP discovery call replaced the source discovery
transaction, and the outgoing connection return value is borrowed. Releasing that borrowed
reference on a page timeout caused an SDK assertion and USB enumeration failure.

The reconnect firmware now relies on SDK automatic source discovery, takes its own
connection reference only on successful connection, preserves L2CAP objects
through SDK disconnect cleanup, and pauses page scanning while paging a peer.
Outgoing pages are serialized. Saved peers retry after timeouts or unexpected
disconnects, with controller paging paused while a headphone ACL is present;
explicit Disconnect stops retries until a new connect command or reboot.
Retries pause during discovery and OTA. The firmware build and 13
JavaScript tests pass; the XZ OTA body matches the application binary byte-for-byte.
The updates were accepted through real HID OTA. The timeout fix survived
HCI page timeout 4 with management USB still responsive and the pairing intact.
The reconnect firmware automatically authenticated Theater U and established
its A2DP signaling link. Diagnostics confirmed five remote endpoints, accepted
SBC configuration and OPEN. Explicit START must wait for media L2CAP
configuration to finish; the media MTU is populated earlier. Incoming signaling
channels retain PSM zero in this SDK and must be handled by the readiness check.

With START accepted, a five-second silent playback received 881848 USB PCM bytes
and submitted 869888 bytes to Bluetooth with no new overflow/audio errors. A
quiet three-second tone received 529048 bytes and submitted 516096 bytes without
new overflow/audio errors. The owner subsequently reported hearing no tone, so these submission counters
do not establish audible playback. Packet-level audio diagnosis is in progress. An
earlier test with controller paging active overflowed 315 times, motivating
the updated retry policy. The final image then automatically authenticated and started streaming after
HID OTA reboot without another pair command. Its five-second playback received
881940 USB PCM bytes and submitted 875520 bytes to Bluetooth, with zero
underflows, overflows or audio errors. One USB gamepad diagnostic cycle passed
for both slots, including isolation, simultaneous reports and automatic release;
the measured RTOS rate was 989.7 ticks/second. These synthetic USB checks do not
substitute for physical DualSense input. A passive input watch received zero
physical events, and the controller is currently paired but disconnected.
One DualSense ACL connection was observed, followed by remote disconnection;
physical input and concurrent streaming remain unverified. The owner reports
that the saved DualSense still does not reconnect. Inspection found that the
application also opened outgoing HID channels on controller-initiated
connections, conflicting with the controller opening its own channels; the
next candidate restricts outgoing HID setup to locally initiated connections.
A read-only UART recovery attempt could not complete the ROM handshake; it
did not write flash or obtain a backup. A reachable headphone and physical
reset recovered USB for OTA. Boot2 and partition metadata were not programmed
through UART.

| Current firmware artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Application | 632416 | `9c4a5c52e292d99c0f7fcc02abb560b3a3df05ee928e3515cf7358b5d5f898b0` |
| XZ OTA | 355064 | `9ddffc3200b9a6cc777e8d1739f74b323da42ec9f3c94c868f28dd5d17862f04` |

The following November 15 baseline predates that investigation.

On November 15, the owner flashed the radio/audio image. Its UART build banner
is `23:08:23, Nov 15 2026`; the current local binary/package match byte-for-byte
when the OTA body is decompressed. Boot2 reports FW active index 0, table age 30,
FW age 18. The Bluetooth controller starts successfully, with local address
`B4:0E:CF:35:9B:C8` and device capabilities `0x07`.

| Real-device check | Result |
| --- | --- |
| USB descriptors | High speed 480 Mbps; UAC1 stereo 44.1 kHz, S16_LE, asynchronous feedback; two HID gamepads |
| Management HID | Device state, empty-slot rejection, OTA framing probe and maximum-size fragmented request passed |
| Actual RTOS tick rate | 1010.9 ticks/second; expected 1000, within host measurement tolerance |
| Both USB gamepads | Two diagnostic cycles: axes, buttons, hat, slot isolation, simultaneous reports and automatic 500 ms release passed |
| Bluetooth discovery | Inquiry completes without disconnect; first scan found one unidentified distant device; subsequent scan found none |
| USB audio reception | Three-second silent ALSA playback received 529196 bytes, zero overflow/errors; a second five-second check also passed |
| Actual DualSense pairing/input | Pairing and connection subsequently observed; physical input still pending |
| Actual Theater U pairing/playback | Authentication, saved pairing and SBC streaming observed; PCM delivery tests passed; hearing confirmation pending |
| Reconnect and concurrent controller/headphone | Final headphone automatic authentication/streaming after OTA reboot passed; concurrent physical controller input pending |

All 38 host tests pass (8 C, 17 Python, 13 JavaScript). Browser discovery and
pairing flow was checked using a mocked HID transport; direct browser-to-device
interaction is separate from the real Linux HID feature-report checks above.

### UART control-line pitfall

The earlier USB disappearance was traced to opening the CH340 UART with default
asserted DTR/RTS lines. Creating `Serial(port=None)`, deasserting both lines, and
then opening the port restored full boot output and stable USB/radio operation.
Use [uart_monitor.py](../tools/uart_monitor.py), for example:

```sh
python3 tools/uart_monitor.py --port /dev/ttyUSB0 --duration 30
```

A continuity test found that the Linux CH340 driver still briefly resets the
board when the port opens, even with both lines preset false. Start the logger
before connecting HID/audio clients and keep it open through the test; opening
it during a test invalidates the existing HID handle and requires reconnection.

The older package was mistakenly reapplied during initial preparation because
`post_build` alone does not refresh OTA files. The owner subsequently flashed
the new image. Run the SDK `combine` target after a successful build and
`post_build`; verify its XZ body against the current binary before uploading.

### Repeating hardware checks

Run these with the necessary hidraw, input and sound device permissions. The
gamepad checker sends transient synthetic input; run it while no real controller
is sending input. These checks preserve pairing records and do not write flash.

```sh
python3 tools/usb_hid_check.py --device /dev/hidraw0
python3 tools/usb_gamepad_check.py --hid /dev/hidraw0 --cycles 2
python3 tools/usb_audio_check.py --device /dev/hidraw0 --seconds 5
# After an A2DP stream connects, play a louder audible test tone (-12 dBFS default amplitude 8000) and inspect PCM/SBC metrics:
python3 tools/usb_audio_check.py --device /dev/hidraw0 --seconds 3 --tone
python3 tools/usb_audio_check.py --device /dev/hidraw0 --seconds 3 --inspect-capture
python3 tools/bluetooth_check.py --device /dev/hidraw0 --scan
```

Use one HID management client at a time. The audio checker resolves the ALSA
card from the selected dongle's USB identity and plays silence; it does not
prove audible playback through an unpaired headphone. Live audio counters
separate USB PCM reception from PCM submitted to the Bluetooth encoder.

Historical November 15 image (USB/radio startup verified):

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Application | 628240 | `110946546a2a9423c42cd14b4ed39ef45f0726fdc1eab3259980a56ad85312bf` |
| XZ OTA | 351824 | `11d36c1bcf3532c56c47203a8b008e304ac4bc41cecfc3a30e7d5da580bf7620` |
