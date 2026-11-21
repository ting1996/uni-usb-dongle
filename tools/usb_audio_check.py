#!/usr/bin/env python3
"""Play silence or a test tone through the dongle's audio device and verify PCM counters.

On Linux, requires alsa-utils and access to management hidraw and /dev/snd nodes.
On Windows, uses the native Windows Multimedia (winmm.dll) waveOut endpoint and
the Windows HID management interface.
This verifies USB reception; headphone playback additionally requires pairing.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import time

if __package__:
    from . import usb_hid_check
    from .usb_hid_check import HidPort
    from .bluetooth_check import radio_state
    from .usb_gamepad_check import usb_device_path
    from .usb_device_check import _request
else:
    import usb_hid_check
    from usb_hid_check import HidPort
    from bluetooth_check import radio_state
    from usb_gamepad_check import usb_device_path
    from usb_device_check import _request


def pcm_device(hid_device):
    if os.name == "nt":
        device_id, name = find_windows_waveout_device()
        return f"waveOut:{device_id} ({name})"
    usb = usb_device_path(hid_device, Path("/sys/class/hidraw"))
    cards = [path for path in Path("/sys/class/sound").glob("card[0-9]*")
             if usb in (path / "device").resolve().parents]
    if len(cards) != 1:
        raise ValueError("expected one ALSA card belonging to the selected USB dongle")
    return f"hw:{cards[0].name[4:]},0"


def _dongle_vid_pid_tags(vid=None, pid=None):
    if vid is not None and pid is not None:
        return (f"vid_{vid:04x}&pid_{pid:04x}",)
    if vid is not None:
        return (f"vid_{vid:04x}&",)
    return tuple(f"vid_{v:04x}&pid_{p:04x}" for v, p in usb_hid_check.DONGLE_VID_PIDS)


def sbc_crc8(data, bit_count):
    crc = 0x0F
    for bit_idx in range(bit_count):
        bit = (data[bit_idx >> 3] >> (7 - (bit_idx & 7))) & 1
        msb = (crc >> 7) & 1
        crc = ((crc << 1) ^ (0x1D if (msb ^ bit) else 0)) & 0xFF
    return crc


def analyze_sbc_rtp_packet(packet):
    if len(packet) < 17 or packet[0] != 0x80 or (packet[1] & 0x7F) != 96:
        raise ValueError("invalid SBC RTP packet header")
    if not packet[12] or (packet[12] & 0xF0) != 0:
        raise ValueError("invalid or fragmented SBC media payload header")

    rtp_seq, rtp_timestamp, ssrc = struct.unpack_from(">HII", packet, 2)
    frame_count = packet[12]
    rates = (16000, 32000, 44100, 48000)
    modes = ("mono", "dual", "stereo", "joint_stereo")
    pos = 13
    max_scale_factor = 0
    first_meta = None

    for frame_index in range(frame_count):
        if pos + 4 > len(packet) or packet[pos] != 0x9C:
            raise ValueError(f"missing SBC syncword at frame {frame_index}")
        hdr1 = packet[pos + 1]
        bitpool = packet[pos + 2]
        crc_stored = packet[pos + 3]
        freq_hz = rates[(hdr1 >> 6) & 3]
        blocks = 4 * (((hdr1 >> 4) & 3) + 1)
        mode_idx = (hdr1 >> 2) & 3
        channels = 2 if mode_idx else 1
        alloc = "snr" if (hdr1 & 2) else "loudness"
        subbands = 8 if (hdr1 & 1) else 4
        bits = blocks * bitpool * (channels if mode_idx < 2 else 1) + (
            subbands if mode_idx == 3 else 0
        )
        frame_bytes = 4 + (subbands * channels) // 2 + (bits + 7) // 8
        if pos + frame_bytes > len(packet):
            raise ValueError(f"truncated SBC frame {frame_index}")

        join_bits = subbands if mode_idx == 3 else 0
        sf_bits = 4 * channels * subbands
        crc_payload_bytes = (join_bits + sf_bits + 7) // 8
        if 4 + crc_payload_bytes > frame_bytes:
            raise ValueError(f"invalid SBC frame {frame_index} length")
        crc_input = bytes((hdr1, bitpool)) + packet[pos + 4 : pos + 4 + crc_payload_bytes]
        crc_calc = sbc_crc8(crc_input, 16 + join_bits + sf_bits)
        if crc_calc != crc_stored:
            raise ValueError(
                f"SBC CRC-8 mismatch at frame {frame_index}: expected 0x{crc_calc:02x}, got 0x{crc_stored:02x}"
            )

        sf_start_byte = pos + 4 + (1 if mode_idx == 3 and subbands == 8 else 0)
        if mode_idx == 3 and subbands == 4:
            # First 4 bits are join flags, then alternating high/low nibbles.
            for nibble_idx in range(channels * subbands):
                bit_off = 4 + nibble_idx * 4
                byte_val = packet[pos + 4 + (bit_off >> 3)]
                sf = (byte_val >> 4) & 0x0F if (bit_off & 7) == 0 else byte_val & 0x0F
                if sf > max_scale_factor:
                    max_scale_factor = sf
        else:
            for byte_idx in range((channels * subbands) // 2):
                b = packet[sf_start_byte + byte_idx]
                max_scale_factor = max(max_scale_factor, (b >> 4) & 0x0F, b & 0x0F)

        if first_meta is None:
            first_meta = (freq_hz, blocks, modes[mode_idx], alloc, subbands, bitpool, frame_bytes)
        pos += frame_bytes

    if pos != len(packet):
        raise ValueError(f"unexpected trailing bytes in SBC RTP packet ({len(packet) - pos} bytes)")

    return {
        "rtp_seq": rtp_seq,
        "rtp_timestamp": rtp_timestamp,
        "ssrc": ssrc,
        "frame_count": frame_count,
        "sample_rate_hz": first_meta[0],
        "blocks": first_meta[1],
        "channel_mode": first_meta[2],
        "allocation": first_meta[3],
        "subbands": first_meta[4],
        "bitpool": first_meta[5],
        "frame_bytes": first_meta[6],
        "max_scale_factor": max_scale_factor,
        "has_audio_energy": max_scale_factor > 0,
    }


def read_audio_metrics(port, sequence=450, timeout=3.0):
    data = _request(port, usb_hid_check.CMD_GET_AUDIO_METRICS, sequence, b"", timeout)
    if data and data[0] in (2, 8):
        return None
    if len(data) != 12 or data[0] != 0:
        raise ValueError(f"invalid audio metrics response: {data.hex()}")
    peak_l, peak_r, nonzero_packets = struct.unpack_from("<HHI", data, 3)
    return {
        "avrcp_connected": bool(data[1]),
        "avrcp_volume": data[2],
        "peak_l": peak_l,
        "peak_r": peak_r,
        "nonzero_packets": nonzero_packets,
    }


def _find_windows_wave_device(is_input, vid=None, pid=None):
    """Locate a Windows Multimedia waveOut or waveIn device belonging to the USB dongle."""
    winmm = ctypes.WinDLL("winmm", use_last_error=True)

    class WAVECAPS_BASE(ctypes.Structure):
        _fields_ = [
            ("wMid", wintypes.WORD),
            ("wPid", wintypes.WORD),
            ("vDriverVersion", wintypes.UINT),
            ("szPname", wintypes.WCHAR * 32),
            ("dwFormats", wintypes.DWORD),
            ("wChannels", wintypes.WORD),
            ("wReserved1", wintypes.WORD),
        ]

    class WAVEOUTCAPSW(ctypes.Structure):
        _fields_ = WAVECAPS_BASE._fields_ + [("dwSupport", wintypes.DWORD)]

    caps_cls = WAVECAPS_BASE if is_input else WAVEOUTCAPSW
    get_num = winmm.waveInGetNumDevs if is_input else winmm.waveOutGetNumDevs
    get_caps = winmm.waveInGetDevCapsW if is_input else winmm.waveOutGetDevCapsW
    msg_fn = winmm.waveInMessage if is_input else winmm.waveOutMessage

    get_num.argtypes = []
    get_num.restype = wintypes.UINT
    get_caps.argtypes = [ctypes.c_size_t, ctypes.POINTER(caps_cls), wintypes.UINT]
    get_caps.restype = wintypes.UINT
    msg_fn.argtypes = [ctypes.c_void_p, wintypes.UINT, ctypes.c_size_t, ctypes.c_size_t]
    msg_fn.restype = wintypes.UINT

    DRV_QUERYDEVICEINTERFACE = 0x080C
    DRV_QUERYDEVICEINTERFACESIZE = 0x080D
    tags = _dongle_vid_pid_tags(vid, pid)

    count = get_num()
    fallback = None
    for index in range(count):
        caps = caps_cls()
        if get_caps(index, ctypes.byref(caps), ctypes.sizeof(caps)) != 0:
            continue
        buf_len = wintypes.DWORD(0)
        rc = msg_fn(
            ctypes.c_void_p(index), DRV_QUERYDEVICEINTERFACESIZE,
            ctypes.cast(ctypes.byref(buf_len), ctypes.c_void_p).value, 0,
        )
        if rc == 0 and buf_len.value > 0:
            iface_buf = ctypes.create_unicode_buffer((buf_len.value // 2) + 2)
            if msg_fn(
                ctypes.c_void_p(index), DRV_QUERYDEVICEINTERFACE,
                ctypes.cast(iface_buf, ctypes.c_void_p).value, buf_len.value,
            ) == 0:
                iface_lower = iface_buf.value.lower()
                if any(tag in iface_lower for tag in tags):
                    return index, caps.szPname
        if fallback is None and ("Bluetooth Gamepads" in caps.szPname or "Uni USB Dongle" in caps.szPname):
            fallback = (index, caps.szPname)

    if fallback is not None:
        return fallback
    kind = "waveIn mic" if is_input else "waveOut audio"
    raise ValueError(f"expected one Windows {kind} device belonging to the USB dongle")


def find_windows_waveout_device(vid=None, pid=None):
    """Locate the Windows Multimedia waveOut device ID for the USB dongle."""
    return _find_windows_wave_device(False, vid=vid, pid=pid)


def find_windows_wavein_device(vid=None, pid=None):
    """Locate the Windows Multimedia waveIn microphone device ID for the USB dongle."""
    return _find_windows_wave_device(True, vid=vid, pid=pid)


def _winmm_wave_types():
    class WAVEFORMATEX(ctypes.Structure):
        _pack_ = 1
        _fields_ = [
            ("wFormatTag", wintypes.WORD),
            ("nChannels", wintypes.WORD),
            ("nSamplesPerSec", wintypes.DWORD),
            ("nAvgBytesPerSec", wintypes.DWORD),
            ("nBlockAlign", wintypes.WORD),
            ("wBitsPerSample", wintypes.WORD),
            ("cbSize", wintypes.WORD),
        ]

    class WAVEHDR(ctypes.Structure):
        _fields_ = [
            ("lpData", ctypes.c_void_p),
            ("dwBufferLength", wintypes.DWORD),
            ("dwBytesRecorded", wintypes.DWORD),
            ("dwUser", ctypes.c_size_t),
            ("dwFlags", wintypes.DWORD),
            ("dwLoops", wintypes.DWORD),
            ("lpNext", ctypes.c_void_p),
            ("reserved", ctypes.c_size_t),
        ]

    return WAVEFORMATEX, WAVEHDR


def play_pcm_windows(device_id, pcm_bytes, sample_rate=44100, channels=2,
                     bits_per_sample=16, timeout=15.0):
    """Play raw S16_LE PCM bytes through a Windows waveOut device synchronously."""
    winmm = ctypes.WinDLL("winmm", use_last_error=True)
    WHDR_DONE = 0x00000001
    WAVE_FORMAT_PCM = 1
    WAVEFORMATEX, WAVEHDR = _winmm_wave_types()

    winmm.waveOutOpen.argtypes = [
        ctypes.POINTER(ctypes.c_void_p), wintypes.UINT, ctypes.POINTER(WAVEFORMATEX),
        ctypes.c_size_t, ctypes.c_size_t, wintypes.DWORD
    ]
    winmm.waveOutOpen.restype = wintypes.UINT
    winmm.waveOutPrepareHeader.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(WAVEHDR), wintypes.UINT
    ]
    winmm.waveOutPrepareHeader.restype = wintypes.UINT
    winmm.waveOutWrite.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(WAVEHDR), wintypes.UINT
    ]
    winmm.waveOutWrite.restype = wintypes.UINT
    winmm.waveOutUnprepareHeader.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(WAVEHDR), wintypes.UINT
    ]
    winmm.waveOutUnprepareHeader.restype = wintypes.UINT
    winmm.waveOutReset.argtypes = [ctypes.c_void_p]
    winmm.waveOutReset.restype = wintypes.UINT
    winmm.waveOutClose.argtypes = [ctypes.c_void_p]
    winmm.waveOutClose.restype = wintypes.UINT

    block_align = channels * (bits_per_sample // 8)
    wfx = WAVEFORMATEX(
        wFormatTag=WAVE_FORMAT_PCM,
        nChannels=channels,
        nSamplesPerSec=sample_rate,
        nAvgBytesPerSec=sample_rate * block_align,
        nBlockAlign=block_align,
        wBitsPerSample=bits_per_sample,
        cbSize=0,
    )
    hwo = ctypes.c_void_p()
    rc = winmm.waveOutOpen(ctypes.byref(hwo), device_id, ctypes.byref(wfx), 0, 0, 0)
    if rc != 0:
        raise OSError(f"waveOutOpen failed with MMRESULT {rc}")

    pcm_buf = ctypes.create_string_buffer(pcm_bytes, len(pcm_bytes))
    hdr = WAVEHDR(
        lpData=ctypes.cast(pcm_buf, ctypes.c_void_p).value,
        dwBufferLength=len(pcm_bytes),
    )
    prepared = False
    try:
        rc = winmm.waveOutPrepareHeader(hwo, ctypes.byref(hdr), ctypes.sizeof(hdr))
        if rc != 0:
            raise OSError(f"waveOutPrepareHeader failed with MMRESULT {rc}")
        prepared = True
        rc = winmm.waveOutWrite(hwo, ctypes.byref(hdr), ctypes.sizeof(hdr))
        if rc != 0:
            raise OSError(f"waveOutWrite failed with MMRESULT {rc}")
        deadline = time.monotonic() + timeout
        while not (hdr.dwFlags & WHDR_DONE):
            if time.monotonic() >= deadline:
                winmm.waveOutReset(hwo)
                raise TimeoutError("timed out waiting for Windows waveOut playback")
            time.sleep(0.02)
    finally:
        if prepared:
            winmm.waveOutUnprepareHeader(hwo, ctypes.byref(hdr), ctypes.sizeof(hdr))
        winmm.waveOutClose(hwo)


def record_pcm_windows(device_id, seconds=1, sample_rate=16000, channels=1,
                       bits_per_sample=16, timeout=10.0):
    """Record raw S16_LE PCM bytes from a Windows waveIn microphone synchronously."""
    winmm = ctypes.WinDLL("winmm", use_last_error=True)
    WHDR_DONE = 0x00000001
    WAVE_FORMAT_PCM = 1
    WAVEFORMATEX, WAVEHDR = _winmm_wave_types()

    winmm.waveInOpen.argtypes = [
        ctypes.POINTER(ctypes.c_void_p), wintypes.UINT, ctypes.POINTER(WAVEFORMATEX),
        ctypes.c_size_t, ctypes.c_size_t, wintypes.DWORD
    ]
    winmm.waveInOpen.restype = wintypes.UINT
    winmm.waveInPrepareHeader.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(WAVEHDR), wintypes.UINT
    ]
    winmm.waveInPrepareHeader.restype = wintypes.UINT
    winmm.waveInAddBuffer.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(WAVEHDR), wintypes.UINT
    ]
    winmm.waveInAddBuffer.restype = wintypes.UINT
    winmm.waveInStart.argtypes = [ctypes.c_void_p]
    winmm.waveInStart.restype = wintypes.UINT
    winmm.waveInStop.argtypes = [ctypes.c_void_p]
    winmm.waveInStop.restype = wintypes.UINT
    winmm.waveInReset.argtypes = [ctypes.c_void_p]
    winmm.waveInReset.restype = wintypes.UINT
    winmm.waveInUnprepareHeader.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(WAVEHDR), wintypes.UINT
    ]
    winmm.waveInUnprepareHeader.restype = wintypes.UINT
    winmm.waveInClose.argtypes = [ctypes.c_void_p]
    winmm.waveInClose.restype = wintypes.UINT

    block_align = channels * (bits_per_sample // 8)
    total_bytes = int(seconds * sample_rate * block_align)
    wfx = WAVEFORMATEX(
        wFormatTag=WAVE_FORMAT_PCM,
        nChannels=channels,
        nSamplesPerSec=sample_rate,
        nAvgBytesPerSec=sample_rate * block_align,
        nBlockAlign=block_align,
        wBitsPerSample=bits_per_sample,
        cbSize=0,
    )
    hwi = ctypes.c_void_p()
    rc = winmm.waveInOpen(ctypes.byref(hwi), device_id, ctypes.byref(wfx), 0, 0, 0)
    if rc != 0:
        raise OSError(f"waveInOpen failed with MMRESULT {rc}")

    pcm_buf = ctypes.create_string_buffer(total_bytes)
    hdr = WAVEHDR(
        lpData=ctypes.cast(pcm_buf, ctypes.c_void_p).value,
        dwBufferLength=total_bytes,
    )
    prepared = False
    try:
        rc = winmm.waveInPrepareHeader(hwi, ctypes.byref(hdr), ctypes.sizeof(hdr))
        if rc != 0:
            raise OSError(f"waveInPrepareHeader failed with MMRESULT {rc}")
        prepared = True
        rc = winmm.waveInAddBuffer(hwi, ctypes.byref(hdr), ctypes.sizeof(hdr))
        if rc != 0:
            raise OSError(f"waveInAddBuffer failed with MMRESULT {rc}")
        rc = winmm.waveInStart(hwi)
        if rc != 0:
            raise OSError(f"waveInStart failed with MMRESULT {rc}")
        deadline = time.monotonic() + timeout
        while not (hdr.dwFlags & WHDR_DONE):
            if time.monotonic() >= deadline:
                raise TimeoutError("timed out waiting for Windows waveIn recording")
            time.sleep(0.02)
        winmm.waveInStop(hwi)
        return pcm_buf.raw[:hdr.dwBytesRecorded]
    finally:
        winmm.waveInReset(hwi)
        if prepared:
            winmm.waveInUnprepareHeader(hwi, ctypes.byref(hdr), ctypes.sizeof(hdr))
        winmm.waveInClose(hwi)


def main():
    default_device = "auto" if os.name == "nt" else "/dev/hidraw0"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default=default_device)
    parser.add_argument("--seconds", type=int, default=3)
    parser.add_argument("--tone", action="store_true", help="play a 440 Hz tone for an audible headphone check")
    parser.add_argument("--amplitude", type=int, default=8000, help="sine wave peak amplitude (1..24000, default 8000)")
    parser.add_argument("--capture", type=Path, help="capture one encoded SBC RTP packet for offline diagnosis")
    parser.add_argument("--inspect-capture", type=Path, help="inspect an existing captured SBC RTP packet offline")
    parser.add_argument("--mic", action="store_true", help="record 16 kHz mono PCM from the Windows waveIn microphone device")
    args = parser.parse_args()
    if args.inspect_capture:
        summary = analyze_sbc_rtp_packet(args.inspect_capture.read_bytes())
        print(json.dumps(summary))
        return
    if args.seconds < 1 or args.seconds > 60:
        parser.error("seconds must be 1..60")
    if args.amplitude < 1 or args.amplitude > 24000:
        parser.error("amplitude must be 1..24000")
    if args.mic:
        if os.name != "nt":
            raise OSError("--mic check currently requires Windows waveIn")
        mic_id, mic_name = find_windows_wavein_device()
        print(f"Recording 16.0 kHz S16_LE mono through waveIn:{mic_id} ({mic_name})", flush=True)
        captured = record_pcm_windows(mic_id, seconds=args.seconds, sample_rate=16000, channels=1,
                                      bits_per_sample=16, timeout=args.seconds + 10)
        received = len(captured)
        expected = args.seconds * 16000 * 2
        if not expected * 0.90 <= received <= expected * 1.10:
            raise ValueError(f"recorded {received} USB mic PCM bytes, expected about {expected}")
        print(f"PASS: recorded {received} USB mic PCM bytes from waveIn:{mic_id} ({mic_name})")
        return
    if os.name == "nt":
        win_dev_id, win_dev_name = find_windows_waveout_device()
        target = f"waveOut:{win_dev_id} ({win_dev_name})"
    else:
        target = pcm_device(args.device)
    with HidPort(args.device) as port:
        before = radio_state(port)
        before_metrics = read_audio_metrics(port, sequence=449)
        if args.capture and _request(port, 0x1c, 300, b"", 3) != b"\0":
            raise ValueError("firmware does not support encoded packet capture")
        if args.tone and not before["audio_authenticated"]:
            raise ValueError("connect and authenticate a headphone before the audible tone check")
        sound = f"440 Hz tone (amp={args.amplitude})" if args.tone else "silence"
        print(f"Playing 44.1 kHz S16_LE stereo {sound} through {target}", flush=True)
        samples = None
        if args.tone:
            samples = bytearray()
            for frame in range(args.seconds * 44100):
                value = round(args.amplitude * math.sin(2 * math.pi * 440 * frame / 44100))
                samples.extend(struct.pack("<hh", value, value))
        if os.name == "nt":
            pcm_bytes = bytes(samples) if samples is not None else bytes(args.seconds * 44100 * 4)
            play_pcm_windows(win_dev_id, pcm_bytes, timeout=args.seconds + 10)
        else:
            subprocess.run(["aplay", "-D", target, "-t", "raw", "-f", "S16_LE",
                            "-c", "2", "-r", "44100", "--period-size=441",
                            "--buffer-size=1764", "-d", str(args.seconds), "-" if args.tone else "/dev/zero"],
                           input=samples, check=True, timeout=args.seconds + 10)
        after = radio_state(port)
        after_metrics = read_audio_metrics(port, sequence=450)
        if args.capture:
            packet = bytearray()
            while True:
                data = _request(port, 0x1d, 400 + len(packet), struct.pack("<H", len(packet)), 3)
                if len(data) < 6 or data[0]:
                    raise ValueError("no encoded SBC packet was captured")
                length, offset = struct.unpack_from("<HH", data, 1)
                if offset != len(packet) or not 14 <= length <= 1024:
                    raise ValueError("invalid encoded packet capture response")
                packet.extend(data[5:])
                if len(packet) >= length: break
            summary = analyze_sbc_rtp_packet(bytes(packet))
            args.capture.write_bytes(packet)
            args.capture.with_suffix(".sbc").write_bytes(packet[13:])
            print(f"Captured {length} encoded RTP bytes ({summary['frame_count']} SBC frames, {summary['sample_rate_hz']} Hz, {summary['channel_mode']}, bitpool={summary['bitpool']}, max_sf={summary['max_scale_factor']})")
    received = (after["usb_bytes"] - before["usb_bytes"]) & 0xffffffff
    sent = (after["bluetooth_bytes"] - before["bluetooth_bytes"]) & 0xffffffff
    expected = args.seconds * 44100 * 4
    if not expected * 0.98 <= received <= expected * 1.02:
        raise ValueError(f"received {received} PCM bytes, expected about {expected}")
    if after["overflows"] != before["overflows"] or after["audio_errors"] != before["audio_errors"]:
        raise ValueError(f"audio counter errors: before={before}, after={after}")
    if before["audio_authenticated"] and sent < expected * 0.90:
        raise ValueError("USB PCM was received but too little was submitted to Bluetooth")
    if after_metrics is not None:
        print(
            f"Audio metrics: avrcp={after_metrics['avrcp_connected']}, "
            f"vol={after_metrics['avrcp_volume']}, peak_l={after_metrics['peak_l']}, "
            f"peak_r={after_metrics['peak_r']}, nonzero_packets={after_metrics['nonzero_packets']}",
            flush=True,
        )
        if args.tone and before_metrics is not None and after_metrics["nonzero_packets"] <= before_metrics["nonzero_packets"]:
            raise ValueError("USB audio tone produced zero non-zero PCM packets on device")
    print(f"PASS: received {received} USB PCM bytes; submitted {sent} bytes to Bluetooth")
    if not before["audio_authenticated"]:
        print("No authenticated headphone was connected: this does not verify headphone playback")
    elif args.tone:
        print("Bluetooth PCM submission passed; audible playback still requires the listener's confirmation")


if __name__ == "__main__":
    main()
