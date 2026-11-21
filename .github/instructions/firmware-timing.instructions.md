---
description: BL616 firmware timer configuration and real-device verification
applyTo: 'firmware/**'
---

## Learnings

The Bouffalo SDK RISC-V FreeRTOS port calculates its MTIME compare increment
from `configCPU_CLOCK_HZ`; in this port the macro must match the timer frequency.
BL616 board startup configures MTIME at **1 MHz**, even though the CPU runs at
320 MHz, so keep `configCPU_CLOCK_HZ` at `1000000UL` for this startup profile.
Using `320000000UL` makes a nominal 500 ms delay take about 160 seconds and
also stretches OTA timeouts and reset delays.

Host RTOS stubs cannot verify the board timer frequency. After changing timer
configuration or startup, run `tools/usb_gamepad_check.py` on the real device:
it compares firmware ticks with host elapsed time and verifies automatic input
release through the Linux joystick nodes. CDC port numbers can change after
OTA; resolve the device's `/dev/serial/by-id/` entry before reconnecting.

The current SDK also defines `portSET_INTERRUPT_MASK_FROM_ISR` and its restore
macro as no-ops. Do not rely on them to protect a buffer shared with the USB
ISR. Task callers must use a critical section; distinguish ISR callbacks from
task/EP0-thread callers with `xPortIsInsideInterrupt()` before using task locks.

The M62 CH340's DTR/RTS lines affect board startup: opening pyserial with its
default asserted lines caused native USB to disappear and UART boot output to
stop. Use `tools/uart_monitor.py`, which creates `Serial(port=None)`, sets both
`dtr` and `rts` to `False`, and only then opens the port. Deasserting the lines
restored a complete boot and Bluetooth inquiry; do not diagnose this symptom as
an OTA or radio crash until modem-control lines have been checked. The Linux
CH340 driver still caused one brief reset even with preconfigured false lines
in a continuity test: start UART monitoring before opening HID/audio clients,
keep the monitor open through the test, and reconnect clients after UART opens.

The audio profile replaces CDC with UAC1; use `tools/usb_gamepad_check.py --hid
/dev/hidraw0` to verify the actual RTOS tick rate and input release in this build.
The SDK `post_build` target alone does not refresh OTA files: run `combine`
after a successful build and verify the XZ OTA body decompresses byte-for-byte
to the current application binary before uploading it.

Bouffalo's `bt_conn_create_br` returns a borrowed sticky reference: take an
application reference in the successful connection callback, and release only
that reference on disconnect. Disable page scanning before outgoing paging and
restore it from the application task after completion; do not reinitialize
L2CAP channel objects inside the ACL disconnect callback while SDK cleanup owns them.

The installed prebuilt A2DP library automatically calls `bt_source_start_discovery`
when its source signaling channel connects. Do not call `bt_start_discovery` from
the chain callback: that API installs the sink discovery callback and can replace
the pending source transaction, producing `Peer mismatch resp` and stalled audio.
After OPEN, explicitly call `bt_stream_resume` from the radio task only after
media L2CAP configuration completes; the media MTU is populated earlier and alone
does not establish readiness (Theater U rejects early START). Incoming signaling
channels retain PSM zero: check for a connected outgoing AVDTP transport and no
pending AVDTP configuration instead of requiring two channels with PSM 0x19.
Unanswered controller pages caused measured PCM overflows during headphone
playback: retain incoming PS-button reconnects rather than paging a missing
controller while the headphone ACL is active.

Incoming DualSense reconnects open HID control/interrupt channels themselves;
only initiate those channels when the dongle initiated the ACL, and use the
reference 672-byte HID MTU. PCM submission counters do not prove audible playback:
capture actual SBC media packets and obtain a listening check. The installed SDK
puts wall-clock milliseconds in SBC RTP timestamps; rewrite them using encoded
sample counts (five 16-block/eight-subband frames advance 640 at 44.1 kHz).

The btprofile 1.8.26 AVDTP sender wraps its transaction label modulo 16, but
`bt_avdtp_open` rejects label zero. A reconnect trace showed SET_CONFIGURATION
label 15 accepted followed by local `Error id` and no OPEN packet. The firmware
wraps OPEN to advance an unused zero label to one before the SDK call. This is
an explicitly version-dependent ABI workaround (label byte 92, BR channel size
76); recheck it when replacing the profile archive. A signaling connection alone
is not a usable audio stream; keep status in profile-connecting until media opens.

The SDK accepts every incoming BR/EDR ACL as peripheral (`accept_conn` in
`hci_core.c` hard-codes `BT_HCI_ROLE_SLAVE`). A PS-button DualSense reconnect
while Theater U streams therefore forms a scatternet: the dongle follows the
controller's piconet clock as well as its own, and SBC packets lose slots
(intermittent dropouts). The radio task requests the central role with HCI
Switch_Role before authentication (one retry after authentication), then
writes a per-link policy without role switch. Check UART for `central link`
versus `remains peripheral` on each ACL. DualSense USB output reports are
coalesced (unchanged reports suppressed for 1 s; at most one per 8 ms, or 16 ms
while streaming) because hosts can resend them every USB frame, consuming
airtime and ACL buffers shared with A2DP.
