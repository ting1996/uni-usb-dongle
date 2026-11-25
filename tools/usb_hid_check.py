#!/usr/bin/env python3
"""Check the real WebHID feature-report protocol through Linux hidraw or Windows HID.

Default checks do not write flash or pair devices. --image explicitly uploads
an OTA package. On Linux this requires read/write access to the selected
management hidraw node, normally interface 2; on Windows it uses the native
HID class driver (hid.dll / setupapi.dll) on interface 2 collection 2.
"""
import argparse
import ctypes
from ctypes import wintypes
try:
    import fcntl
except ImportError:
    fcntl = None
import os
from pathlib import Path
import struct
import sys
import time

if __package__:
    from . import usb_device_check, usb_ota
else:
    import usb_device_check
    import usb_ota

REPORT_ID = 2
FEATURE_SIZE = 63
FRAGMENT_SIZE = 60
DONGLE_VID = 0xFFFF
DONGLE_PID = 0xFFFF
DONGLE_VID_PIDS = (
    (0xFFFF, 0xFFFF),
    (0x045E, 0x028E),
    (0x054C, 0x09CC),
    (0x057E, 0x2009),
    (0x054C, 0x0CE6),
)
MGMT_USAGE_PAGE = 0xFF00
MGMT_USAGE = 0x0001
CMD_GET_PROFILE_STATE = 0x1E
CMD_SET_PROFILE = 0x1F
CMD_GET_AUDIO_METRICS = 0x20
CMD_GET_DUALSENSE_METRICS = 0x21
CMD_GET_MIC_CONFIG = 0x22
CMD_SET_MIC_SOURCE = 0x23
CMD_REMOVE_AUDIO = 0x24
CMD_GET_BLOCKLIST = 0x25
CMD_BLOCK_DEVICE = 0x26
CMD_UNBLOCK_DEVICE = 0x27
PROFILE_NAMES = {0: "generic", 1: "xinput", 2: "ps4", 3: "switch", 4: "dualsense"}
PROFILE_IDS = {name: idx for idx, name in PROFILE_NAMES.items()}
CODEC_NAMES = {0: "auto", 1: "sbc", 2: "aac", 3: "ldac"}
CODEC_IDS = {name: idx for idx, name in CODEC_NAMES.items()}
MIC_SOURCE_NAMES = {0: "auto", 1: "headphone", 2: "controller0", 3: "controller1", 4: "off"}
MIC_SOURCE_IDS = {name: idx for idx, name in MIC_SOURCE_NAMES.items()}
MIC_CODEC_NAMES = {0: "none", 1: "cvsd", 2: "msbc", 3: "opus"}


def feature_fragments(packet):
    for offset in range(0, len(packet), FRAGMENT_SIZE):
        data = packet[offset:offset + FRAGMENT_SIZE]
        report = bytearray(64)
        report[0] = REPORT_ID
        struct.pack_into("<HB", report, 1, offset, len(data))
        report[4:4 + len(data)] = data
        yield report


def decode_feature(report):
    if len(report) != 64 or report[0] != REPORT_ID:
        raise ValueError("invalid HID feature report ID/size")
    offset, size = struct.unpack_from("<HB", report, 1)
    if offset != 0 or size > FRAGMENT_SIZE:
        raise ValueError("invalid HID response offset/size")
    packet = bytes(report[4:4 + size])
    if packet:
        usb_ota.decode_frame(packet)
    return packet


if os.name == "nt":
    _INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
    _DIGCF_PRESENT = 0x02
    _DIGCF_DEVICEINTERFACE = 0x10
    _GENERIC_READ = 0x80000000
    _GENERIC_WRITE = 0x40000000
    _FILE_SHARE_READ = 0x01
    _FILE_SHARE_WRITE = 0x02
    _OPEN_EXISTING = 3
    _FILE_FLAG_OVERLAPPED = 0x40000000

    class _GUID(ctypes.Structure):
        _fields_ = [
            ("Data1", wintypes.DWORD),
            ("Data2", wintypes.WORD),
            ("Data3", wintypes.WORD),
            ("Data4", ctypes.c_ubyte * 8),
        ]

    class _SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
        _fields_ = [
            ("cbSize", wintypes.DWORD),
            ("InterfaceClassGuid", _GUID),
            ("Flags", wintypes.DWORD),
            ("Reserved", ctypes.c_size_t),
        ]

    class _HIDD_ATTRIBUTES(ctypes.Structure):
        _fields_ = [
            ("Size", wintypes.ULONG),
            ("VendorID", wintypes.USHORT),
            ("ProductID", wintypes.USHORT),
            ("VersionNumber", wintypes.USHORT),
        ]

    class _HIDP_CAPS(ctypes.Structure):
        _fields_ = [
            ("Usage", wintypes.USHORT),
            ("UsagePage", wintypes.USHORT),
            ("InputReportByteLength", wintypes.USHORT),
            ("OutputReportByteLength", wintypes.USHORT),
            ("FeatureReportByteLength", wintypes.USHORT),
            ("Reserved", wintypes.USHORT * 17),
            ("NumberLinkCollectionNodes", wintypes.USHORT),
            ("NumberInputButtonCaps", wintypes.USHORT),
            ("NumberInputValueCaps", wintypes.USHORT),
            ("NumberInputDataIndices", wintypes.USHORT),
            ("NumberOutputButtonCaps", wintypes.USHORT),
            ("NumberOutputValueCaps", wintypes.USHORT),
            ("NumberOutputDataIndices", wintypes.USHORT),
            ("NumberFeatureButtonCaps", wintypes.USHORT),
            ("NumberFeatureValueCaps", wintypes.USHORT),
            ("NumberFeatureDataIndices", wintypes.USHORT),
        ]

    def _win_apis():
        hid = ctypes.WinDLL("hid", use_last_error=True)
        setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

        hid.HidD_GetHidGuid.argtypes = [ctypes.POINTER(_GUID)]
        hid.HidD_GetHidGuid.restype = None
        hid.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.POINTER(_HIDD_ATTRIBUTES)]
        hid.HidD_GetAttributes.restype = wintypes.BOOLEAN
        hid.HidD_GetPreparsedData.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
        hid.HidD_GetPreparsedData.restype = wintypes.BOOLEAN
        hid.HidD_FreePreparsedData.argtypes = [ctypes.c_void_p]
        hid.HidD_FreePreparsedData.restype = wintypes.BOOLEAN
        hid.HidP_GetCaps.argtypes = [ctypes.c_void_p, ctypes.POINTER(_HIDP_CAPS)]
        hid.HidP_GetCaps.restype = ctypes.c_long
        hid.HidD_SetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]
        hid.HidD_SetFeature.restype = wintypes.BOOLEAN
        hid.HidD_GetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]
        hid.HidD_GetFeature.restype = wintypes.BOOLEAN
        hid.HidD_GetInputReport.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]
        hid.HidD_GetInputReport.restype = wintypes.BOOLEAN
        hid.HidD_SetOutputReport.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]
        hid.HidD_SetOutputReport.restype = wintypes.BOOLEAN

        setupapi.SetupDiGetClassDevsW.argtypes = [
            ctypes.POINTER(_GUID), wintypes.LPCWSTR, wintypes.HWND, wintypes.DWORD
        ]
        setupapi.SetupDiGetClassDevsW.restype = ctypes.c_void_p
        setupapi.SetupDiEnumDeviceInterfaces.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(_GUID),
            wintypes.DWORD, ctypes.POINTER(_SP_DEVICE_INTERFACE_DATA),
        ]
        setupapi.SetupDiEnumDeviceInterfaces.restype = wintypes.BOOLEAN
        setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(_SP_DEVICE_INTERFACE_DATA),
            ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
            ctypes.c_void_p,
        ]
        setupapi.SetupDiGetDeviceInterfaceDetailW.restype = wintypes.BOOLEAN
        setupapi.SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]
        setupapi.SetupDiDestroyDeviceInfoList.restype = wintypes.BOOLEAN

        kernel32.CreateFileW.argtypes = [
            wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
            wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
        ]
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.WriteFile.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
        ]
        kernel32.WriteFile.restype = wintypes.BOOLEAN
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.CloseHandle.restype = wintypes.BOOLEAN
        return hid, setupapi, kernel32


def find_windows_hid_devices(vid=None, pid=None):
    """Return metadata dicts for matching Windows HID interfaces."""
    if os.name != "nt":
        return []
    hid, setupapi, kernel32 = _win_apis()
    guid = _GUID()
    hid.HidD_GetHidGuid(ctypes.byref(guid))
    hdev = setupapi.SetupDiGetClassDevsW(
        ctypes.byref(guid), None, None, _DIGCF_PRESENT | _DIGCF_DEVICEINTERFACE
    )
    if not hdev or hdev == _INVALID_HANDLE_VALUE:
        raise ctypes.WinError(ctypes.get_last_error())

    devices = []
    detail_cb_size = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
    try:
        index = 0
        while True:
            did = _SP_DEVICE_INTERFACE_DATA()
            did.cbSize = ctypes.sizeof(_SP_DEVICE_INTERFACE_DATA)
            if not setupapi.SetupDiEnumDeviceInterfaces(
                hdev, None, ctypes.byref(guid), index, ctypes.byref(did)
            ):
                break
            index += 1
            required = wintypes.DWORD(0)
            setupapi.SetupDiGetDeviceInterfaceDetailW(
                hdev, ctypes.byref(did), None, 0, ctypes.byref(required), None
            )
            if required.value < 6:
                continue
            buf = ctypes.create_string_buffer(required.value)
            ctypes.cast(buf, ctypes.POINTER(wintypes.DWORD))[0] = detail_cb_size
            if not setupapi.SetupDiGetDeviceInterfaceDetailW(
                hdev, ctypes.byref(did), buf, required.value, None, None
            ):
                continue
            path = ctypes.wstring_at(ctypes.addressof(buf) + 4)
            handle = kernel32.CreateFileW(
                path,
                _GENERIC_READ | _GENERIC_WRITE,
                _FILE_SHARE_READ | _FILE_SHARE_WRITE,
                None,
                _OPEN_EXISTING,
                0,
                None,
            )
            if handle == _INVALID_HANDLE_VALUE or handle is None:
                continue
            try:
                attrs = _HIDD_ATTRIBUTES()
                attrs.Size = ctypes.sizeof(_HIDD_ATTRIBUTES)
                if not hid.HidD_GetAttributes(handle, ctypes.byref(attrs)):
                    continue
                if vid is None and pid is None:
                    if (attrs.VendorID, attrs.ProductID) not in DONGLE_VID_PIDS:
                        continue
                else:
                    if vid is not None and attrs.VendorID != vid:
                        continue
                    if pid is not None and attrs.ProductID != pid:
                        continue
                preparsed = ctypes.c_void_p()
                if not hid.HidD_GetPreparsedData(handle, ctypes.byref(preparsed)):
                    continue
                try:
                    caps = _HIDP_CAPS()
                    if hid.HidP_GetCaps(preparsed, ctypes.byref(caps)) < 0:
                        continue
                finally:
                    hid.HidD_FreePreparsedData(preparsed)
                devices.append({
                    "path": path,
                    "vid": attrs.VendorID,
                    "pid": attrs.ProductID,
                    "version": attrs.VersionNumber,
                    "usage_page": caps.UsagePage,
                    "usage": caps.Usage,
                    "input_len": caps.InputReportByteLength,
                    "output_len": caps.OutputReportByteLength,
                    "feature_len": caps.FeatureReportByteLength,
                    "input_button_caps": caps.NumberInputButtonCaps,
                    "input_value_caps": caps.NumberInputValueCaps,
                })
            finally:
                kernel32.CloseHandle(handle)
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(hdev)
    return devices


def _resolve_windows_mgmt_path(device, devices=None):
    if not device or device == "auto" or device.startswith("/dev/"):
        candidates = find_windows_hid_devices() if devices is None else devices
        matches = [
            item for item in candidates
            if item["usage_page"] == MGMT_USAGE_PAGE
            and item["usage"] == MGMT_USAGE
            and item["feature_len"] == 64
        ]
        if not matches:
            raise OSError(
                "no uni-usb-dongle Windows HID management interface "
                "(usagePage=0xFF00 usage=1) was found"
            )
        if len(matches) > 1:
            raise OSError("multiple uni-usb-dongle management HID devices found; specify --device path")
        return matches[0]["path"]
    return device


def decode_profile_state(data):
    if not data or not (10 <= len(data) <= 12) or data[0] != 0:
        raise ValueError(f"invalid profile state response: {data.hex() if data else ''}")
    saved_profile = data[10] if len(data) >= 11 else 0
    flags = data[11] if len(data) >= 12 else 0
    return {
        "active": data[2],
        "active_name": PROFILE_NAMES.get(data[2], f"unknown({data[2]})"),
        "requested": data[3],
        "requested_name": PROFILE_NAMES.get(data[3], f"unknown({data[3]})"),
        "profile_mask": data[4],
        "codec": data[5],
        "codec_name": CODEC_NAMES.get(data[5], f"unknown({data[5]})"),
        "streaming": None if data[6] == 0xFF else data[6],
        "codec_mask": data[7],
        "transition": data[8],
        "failure": data[9],
        "saved_profile": saved_profile,
        "saved_profile_name": PROFILE_NAMES.get(saved_profile, f"unknown({saved_profile})"),
        "persisted": bool(flags & 0x01),
        "two_controller_mode": bool(flags & 0x02),
    }


def read_profile_state(port, sequence=101, timeout=3.0):
    data = usb_device_check._request(port, CMD_GET_PROFILE_STATE, sequence, b"", timeout)
    if data and data[0] in (2, 8):
        return None
    return decode_profile_state(data)


def set_profile(port, profile, codec=0, persist=False, sequence=102, timeout=3.0):
    payload = bytes((profile, codec, 1)) if persist else bytes((profile, codec))
    data = usb_device_check._request(
        port, CMD_SET_PROFILE, sequence, payload, timeout
    )
    if not data or data[0] != 0:
        raise ValueError(f"profile switch rejected: {data.hex() if data else ''}")
    return decode_profile_state(data)


def decode_dualsense_metrics(data):
    if not data or (len(data) != 30 and len(data) < 38) or data[0] != 0:
        raise ValueError(f"invalid DualSense metrics response: {data.hex() if data else ''}")
    bt_in_31, bt_in_01, usb_out_02, bt_out_31_ok, bt_out_31_err, bt_feat_set_ok, bt_feat_get_ok = (
        struct.unpack_from("<5I2H", data, 4)
    )
    decoded = {
        "slot": data[1],
        "conn_flags": data[2],
        "feat_mask": data[3],
        "bt_in_31": bt_in_31,
        "bt_in_01": bt_in_01,
        "usb_out_02": usb_out_02,
        "bt_out_31_ok": bt_out_31_ok,
        "bt_out_31_err": bt_out_31_err,
        "bt_feat_set_ok": bt_feat_set_ok,
        "bt_feat_get_ok": bt_feat_get_ok,
        "last_feat_id": data[28],
        "last_handshake": data[29],
        "last_valid_flag0": None,
        "last_valid_flag1": None,
        "last_motor_right": None,
        "last_motor_left": None,
        "last_right_trigger_mode": None,
        "last_left_trigger_mode": None,
        "last_out_path_flags": None,
        "fw_ver_lo": None,
    }
    if len(data) >= 38:
        decoded.update({
            "last_valid_flag0": data[30],
            "last_valid_flag1": data[31],
            "last_motor_right": data[32],
            "last_motor_left": data[33],
            "last_right_trigger_mode": data[34],
            "last_left_trigger_mode": data[35],
            "last_out_path_flags": data[36],
            "fw_ver_lo": data[37],
        })
    return decoded


def read_dualsense_metrics(port, slot=0, sequence=103, timeout=3.0):
    data = usb_device_check._request(
        port, CMD_GET_DUALSENSE_METRICS, sequence, bytes((slot,)), timeout
    )
    if data and data[0] in (2, 8):
        return None
    return decode_dualsense_metrics(data)


def parse_mac_address(value):
    parts = value.split(":")
    if len(parts) != 6 or any(len(p) != 2 for p in parts):
        raise ValueError(f"invalid Bluetooth MAC address: {value!r}")
    return bytes(int(p, 16) for p in parts)


def decode_mic_state(data):
    if not data or len(data) != 16 or data[0] != 0:
        raise ValueError(f"invalid mic config response: {data.hex() if data else ''}")
    flags = data[4]
    mic_peak, decoded_frames, error_frames = struct.unpack_from("<HII", data, 6)
    return {
        "configured_source": data[1],
        "configured_source_name": MIC_SOURCE_NAMES.get(data[1], f"unknown({data[1]})"),
        "active_source": data[2],
        "active_source_name": MIC_SOURCE_NAMES.get(data[2], f"unknown({data[2]})"),
        "active_codec": data[3],
        "active_codec_name": MIC_CODEC_NAMES.get(data[3], f"unknown({data[3]})"),
        "flags": flags,
        "usb_open": bool(flags & 0x01),
        "usb_muted": bool(flags & 0x02),
        "hfp_slc_ready": bool(flags & 0x04),
        "hfp_sco_open": bool(flags & 0x08),
        "ds0_connected": bool(flags & 0x10),
        "ds1_connected": bool(flags & 0x20),
        "sco_callback_seen": bool(flags & 0x40),
        "mic_peak": mic_peak,
        "decoded_frames": decoded_frames,
        "error_frames": error_frames,
    }


def read_mic_state(port, sequence=106, timeout=3.0):
    data = usb_device_check._request(port, CMD_GET_MIC_CONFIG, sequence, b"", timeout)
    if data and data[0] in (2, 8):
        return None
    return decode_mic_state(data)


def set_mic_source(port, source, sequence=107, timeout=3.0):
    data = usb_device_check._request(
        port, CMD_SET_MIC_SOURCE, sequence, bytes((source,)), timeout
    )
    if not data or data[0] != 0:
        raise ValueError(f"mic source update rejected: {data.hex() if data else ''}")
    return decode_mic_state(data)


def remove_audio(port, sequence=108, timeout=3.0):
    data = usb_device_check._request(port, CMD_REMOVE_AUDIO, sequence, b"", timeout)
    if data and data[0] in (2, 8):
        return None
    if not data or data[0] != 0:
        raise ValueError(f"remove audio rejected: {data.hex() if data else ''}")
    return True


def decode_blocklist(data):
    if not data or len(data) < 3 or data[0] != 0:
        raise ValueError(f"invalid blocklist response: {data.hex() if data else ''}")
    count = data[1]
    addr_len = data[2]
    if addr_len != 6 or len(data) != 3 + count * 6:
        raise ValueError(f"malformed blocklist payload length: {data.hex()}")
    return [
        ":".join(f"{b:02X}" for b in data[3 + i * 6 : 3 + (i + 1) * 6])
        for i in range(count)
    ]


def read_blocklist(port, sequence=109, timeout=3.0):
    data = usb_device_check._request(port, CMD_GET_BLOCKLIST, sequence, b"", timeout)
    if data and data[0] in (2, 8):
        return None
    return decode_blocklist(data)


def block_device(port, mac_bytes, sequence=110, timeout=3.0):
    data = usb_device_check._request(port, CMD_BLOCK_DEVICE, sequence, mac_bytes, timeout)
    if not data or data[0] != 0:
        raise ValueError(f"block device rejected: {data.hex() if data else ''}")
    return decode_blocklist(data)


def unblock_device(port, mac_bytes, sequence=111, timeout=3.0):
    data = usb_device_check._request(port, CMD_UNBLOCK_DEVICE, sequence, mac_bytes, timeout)
    if not data or data[0] != 0:
        raise ValueError(f"unblock device rejected: {data.hex() if data else ''}")
    return decode_blocklist(data)


def _format_mac_le(raw):
    return ":".join(f"{b:02x}" for b in reversed(raw))


def run_dualsense_passthrough_test(port):
    if os.name != "nt":
        raise OSError("--dualsense-passthrough-test currently requires Windows hid.dll")
    candidates = sorted(
        (
            d for d in find_windows_hid_devices(0x054C, 0x0CE6)
            if d["usage_page"] == 1 and d["usage"] == 5
        ),
        key=lambda d: d["path"].lower(),
    )
    if not candidates:
        raise OSError("no DualSense (054c:0ce6) HID gamepad interface found")
    dev = candidates[0]
    if dev["input_len"] != 64 or dev["output_len"] != 48 or dev["feature_len"] != 64:
        raise ValueError(f"unexpected DualSense HID caps: {dev}")

    before = read_dualsense_metrics(port, slot=0, sequence=104)
    if before is None:
        raise ValueError("DualSense telemetry command (0x21) unavailable on running firmware")

    hid, _, kernel32 = _win_apis()
    handle = kernel32.CreateFileW(
        dev["path"],
        _GENERIC_READ | _GENERIC_WRITE,
        _FILE_SHARE_READ | _FILE_SHARE_WRITE,
        None,
        _OPEN_EXISTING,
        0,
        None,
    )
    if handle == _INVALID_HANDLE_VALUE or handle is None:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        in_buf = (ctypes.c_ubyte * 64)()
        in_buf[0] = 0x01
        if not hid.HidD_GetInputReport(handle, in_buf, 64):
            raise ctypes.WinError(ctypes.get_last_error())
        in_bytes = bytes(in_buf)
        print(
            f"DualSense input 0x01: sticks=({in_bytes[1]},{in_bytes[2]},{in_bytes[3]},{in_bytes[4]}) "
            f"triggers=({in_bytes[5]},{in_bytes[6]}) btns=({in_bytes[8]:02x},{in_bytes[9]:02x},{in_bytes[10]:02x}) "
            f"batt=0x{in_bytes[53]:02x}"
        )

        f05 = (ctypes.c_ubyte * 41)()
        f05[0] = 0x05
        if not hid.HidD_GetFeature(handle, f05, 41):
            raise ctypes.WinError(ctypes.get_last_error())

        f09 = (ctypes.c_ubyte * 20)()
        f09[0] = 0x09
        if not hid.HidD_GetFeature(handle, f09, 20):
            raise ctypes.WinError(ctypes.get_last_error())
        f09_b = bytes(f09)

        f20 = (ctypes.c_ubyte * 64)()
        f20[0] = 0x20
        if not hid.HidD_GetFeature(handle, f20, 64):
            raise ctypes.WinError(ctypes.get_last_error())
        f20_b = bytes(f20)
        fw_date = f20_b[1:12].split(b"\x00", 1)[0].decode("ascii", "replace")
        fw_time = f20_b[12:20].split(b"\x00", 1)[0].decode("ascii", "replace")
        hw_info = struct.unpack_from("<I", f20_b, 24)[0]
        main_ver = struct.unpack_from("<I", f20_b, 28)[0]
        print(
            f"DualSense features: dev_mac={_format_mac_le(f09_b[1:7])} "
            f"host_mac={_format_mac_le(f09_b[10:16])} fw='{fw_date} {fw_time}' "
            f"hw=0x{hw_info:08x} ver=0x{main_ver:08x}"
        )

        # Read-only serial query: SET_FEATURE 0x80 [0x01, 0x13] -> poll GET_FEATURE 0x81.
        f80 = (ctypes.c_ubyte * 64)()
        f80[0] = 0x80
        f80[1] = 0x01
        f80[2] = 0x13
        if not hid.HidD_SetFeature(handle, f80, 64):
            raise ctypes.WinError(ctypes.get_last_error())
        f81_b = b""
        for _ in range(10):
            time.sleep(0.025)
            f81 = (ctypes.c_ubyte * 64)()
            f81[0] = 0x81
            if not hid.HidD_GetFeature(handle, f81, 64):
                raise ctypes.WinError(ctypes.get_last_error())
            f81_b = bytes(f81)
            if f81_b[3] != 0x01:
                break
        serial_txt = bytes(b for b in f81_b[4:22] if 32 <= b < 127).decode("ascii", "replace")
        print(
            f"DualSense feature 0x80->0x81: cmd=({f81_b[1]:02x},{f81_b[2]:02x}) "
            f"status=0x{f81_b[3]:02x} text='{serial_txt}' hex={f81_b[4:24].hex()}"
        )

        out = bytearray(48)
        out[0] = 0x02
        out[1] = 0x03  # COMPATIBLE_VIBRATION | HAPTICS_SELECT
        out[2] = 0x04 | 0x08 | 0x10  # R2 trigger | L2 trigger | player LED
        out[3] = 0x40  # right motor
        out[4] = 0x20  # left motor
        out[11] = 0x01  # R2 Continuous trigger effect
        out[12] = 0x80
        out[22] = 0x01  # L2 Continuous trigger effect
        out[23] = 0x80
        out[39] = 0x02
        out[42] = 0x02
        out[43] = 0x00
        out[44] = 0x01
        out[45] = 0x00
        out[46] = 0x20
        out[47] = 0xFF
        buf_out = (ctypes.c_ubyte * 48).from_buffer(out)
        written = wintypes.DWORD(0)
        if not kernel32.WriteFile(handle, buf_out, 48, ctypes.byref(written), None) or written.value != 48:
            raise ctypes.WinError(ctypes.get_last_error())

        time.sleep(0.06)
        out_clear = bytearray(48)
        out_clear[0] = 0x02
        out_clear[1] = 0x03
        out_clear[2] = 0x04 | 0x08 | 0x10
        out_clear[11] = 0x05  # Off/reset trigger effect
        out_clear[22] = 0x05
        out_clear[39] = 0x02
        out_clear[42] = 0x02
        out_clear[44] = 0x01
        out_clear[45] = 0x10
        out_clear[46] = 0x40
        out_clear[47] = 0x80
        buf_set = (ctypes.c_ubyte * 48).from_buffer(out_clear)
        if not hid.HidD_SetOutputReport(handle, buf_set, 48):
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel32.CloseHandle(handle)

    time.sleep(0.08)
    after = read_dualsense_metrics(port, slot=0, sequence=105)
    print(f"DualSense passthrough metrics before: {before}")
    print(f"DualSense passthrough metrics after : {after}")
    if after["usb_out_02"] < before["usb_out_02"] + 2:
        raise ValueError(f"usb_out_02 did not increment by >= 2: before={before} after={after}")
    if after["last_out_path_flags"] is not None:
        print(
            f"DualSense v1.1 feedback state: flag0=0x{after['last_valid_flag0']:02x} "
            f"flag1=0x{after['last_valid_flag1']:02x} motors=({after['last_motor_right']},{after['last_motor_left']}) "
            f"triggers=(0x{after['last_right_trigger_mode']:02x},0x{after['last_left_trigger_mode']:02x}) "
            f"path=0x{after['last_out_path_flags']:02x} fw_lo=0x{after['fw_ver_lo']:02x}"
        )
    if after["conn_flags"] & 0x08:
        if after["bt_out_31_ok"] <= before["bt_out_31_ok"]:
            raise ValueError(f"bt_out_31_ok did not increment on connected slot 0: {after}")
        if after["bt_out_31_err"] != before["bt_out_31_err"]:
            raise ValueError(f"bt_out_31_err incremented during passthrough test: {after}")


class HidPort:
    def __init__(self, device="auto"):
        self.fd = None
        self.handle = None
        self.device_path = device
        self.buffer = bytearray()
        self.expected = None
        self.delivered = False

        if os.name == "nt":
            self._hid, _, self._kernel32 = _win_apis()
            path = _resolve_windows_mgmt_path(device)
            self.device_path = path
            handle = self._kernel32.CreateFileW(
                path,
                _GENERIC_READ | _GENERIC_WRITE,
                _FILE_SHARE_READ | _FILE_SHARE_WRITE,
                None,
                _OPEN_EXISTING,
                0,
                None,
            )
            if handle == _INVALID_HANDLE_VALUE or handle is None:
                raise ctypes.WinError(ctypes.get_last_error())
            self.handle = handle
            try:
                preparsed = ctypes.c_void_p()
                if not self._hid.HidD_GetPreparsedData(self.handle, ctypes.byref(preparsed)):
                    raise ctypes.WinError(ctypes.get_last_error())
                try:
                    caps = _HIDP_CAPS()
                    if self._hid.HidP_GetCaps(preparsed, ctypes.byref(caps)) < 0:
                        raise ValueError("unable to query Windows HID device capabilities")
                finally:
                    self._hid.HidD_FreePreparsedData(preparsed)
                if (caps.UsagePage != MGMT_USAGE_PAGE or
                        caps.Usage != MGMT_USAGE or
                        caps.FeatureReportByteLength != 64):
                    raise ValueError("choose the dongle's management HID interface")
            except BaseException:
                self.close()
                raise
            return

        if fcntl is None:
            raise OSError("Linux hidraw (fcntl/ioctl) is unavailable on this platform")
        self.fd = os.open(device, os.O_RDWR | os.O_NONBLOCK)
        try:
            size = bytearray(4)
            fcntl.ioctl(self.fd, 0x80044801, size, True)  # HIDIOCGRDESCSIZE
            count = struct.unpack("<I", size)[0]
            if count > 4096:
                raise ValueError("HID descriptor is too large")
            descriptor = bytearray(4100)
            struct.pack_into("<I", descriptor, 0, count)
            fcntl.ioctl(self.fd, 0x90044802, descriptor, True)  # HIDIOCGRDESC
            data = bytes(descriptor[4:4 + count])
            if b"\x06\x00\xff\x09\x01\xa1\x01\x85\x02" not in data:
                raise ValueError("choose the dongle's management HID interface")
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.handle is not None:
            self._kernel32.CloseHandle(self.handle)
            self.handle = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _set_feature(self, report):
        if os.name == "nt":
            buf = (ctypes.c_ubyte * 64).from_buffer(report)
            if not self._hid.HidD_SetFeature(self.handle, buf, 64):
                raise ctypes.WinError(ctypes.get_last_error())
        else:
            fcntl.ioctl(self.fd, 0xc0404806, report, True)  # HIDIOCSFEATURE(64)

    def _get_feature(self):
        if os.name == "nt":
            buf = (ctypes.c_ubyte * 64)()
            buf[0] = REPORT_ID
            if not self._hid.HidD_GetFeature(self.handle, buf, 64):
                raise ctypes.WinError(ctypes.get_last_error())
            return bytes(buf)
        report = bytearray(64)
        report[0] = REPORT_ID
        fcntl.ioctl(self.fd, 0xc0404807, report, True)  # HIDIOCGFEATURE(64)
        return bytes(report)

    def write(self, packet):
        command, sequence, _ = usb_ota.decode_frame(packet)
        self.expected = (command | usb_ota.RESPONSE_FLAG, sequence)
        self.buffer.clear()
        self.delivered = False
        for report in feature_fragments(packet):
            self._set_feature(report)
        return len(packet)

    def flush(self):
        pass

    def read(self, size):
        deadline = time.monotonic() + 0.1
        while not self.buffer and not self.delivered and time.monotonic() < deadline:
            packet = decode_feature(self._get_feature())
            if packet:
                command, sequence, _ = usb_ota.decode_frame(packet)
                if (command, sequence) == self.expected:
                    self.buffer.extend(packet)
                    self.delivered = True
                    break
            time.sleep(0.005)
        chunk = bytes(self.buffer[:size])
        del self.buffer[:size]
        return chunk


def main():
    default_device = "auto" if os.name == "nt" else "/dev/hidraw0"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--device",
        default=default_device,
        help="management HID interface ('auto' or Windows device path on Windows, /dev/hidrawN on Linux)",
    )
    parser.add_argument("--image", type=Path, help="explicitly upload this OTA package")
    parser.add_argument("--get-profile", action="store_true", help="query active USB output profile")
    parser.add_argument(
        "--set-profile",
        choices=tuple(PROFILE_IDS),
        help="request USB output profile switch",
    )
    parser.add_argument(
        "--persist",
        action="store_true",
        help="save profile passed to --set-profile as persistent boot default",
    )
    parser.add_argument(
        "--codec",
        choices=tuple(CODEC_IDS),
        default="auto",
        help="preferred Bluetooth audio codec when using --set-profile",
    )
    parser.add_argument(
        "--mic-config",
        action="store_true",
        help="query Bluetooth microphone routing and decoder metrics (0x22)",
    )
    parser.add_argument(
        "--set-mic-source",
        choices=tuple(MIC_SOURCE_IDS),
        help="set persistent microphone routing source (0x23)",
    )
    parser.add_argument(
        "--remove-audio",
        action="store_true",
        help="remove paired Bluetooth headphone record (0x24)",
    )
    parser.add_argument(
        "--blocklist",
        action="store_true",
        help="query Never Connect blocklist addresses (0x25)",
    )
    parser.add_argument(
        "--block",
        metavar="MAC",
        help="add AA:BB:CC:DD:EE:FF to Never Connect blocklist (0x26)",
    )
    parser.add_argument(
        "--unblock",
        metavar="MAC",
        help="remove AA:BB:CC:DD:EE:FF from Never Connect blocklist (0x27)",
    )
    parser.add_argument(
        "--dualsense-metrics",
        nargs="?",
        const=0,
        type=int,
        metavar="SLOT",
        help="query live DualSense bidirectional passthrough metrics for slot 0 or 1",
    )
    parser.add_argument(
        "--dualsense-passthrough-test",
        action="store_true",
        help="run live Windows 054c:0ce6 input, feature (0x80->0x81), and output (0x02->0x31) test",
    )
    args = parser.parse_args()
    image = args.image.read_bytes() if args.image else None
    if image is not None:
        usb_ota.validate_image(image)
    try:
        with HidPort(args.device) as port:
            before = usb_device_check.check_device(port)
            # Exercise all 18 fragments of a maximum-size frame without flash writes.
            response = usb_device_check._request(
                port, usb_device_check.CMD_GET_DEVICE_STATE, 99, bytes(1024), 3
            )
            if response != b"\x01":
                raise ValueError("oversized state-query payload was not rejected")
            after = usb_device_check.read_device_state(port, sequence=100)
            if before != after:
                raise ValueError("state changed during fragmented HID checks")
            print("Maximum-size fragmented request: rejected safely; state unchanged")
            profile = read_profile_state(port, sequence=101)
            if profile is None:
                print("Output profile: not supported by running firmware (legacy)")
                if args.set_profile or args.get_profile:
                    raise ValueError("output profile commands are unavailable on running firmware")
            else:
                print(
                    f"Output profile: active={profile['active']} ({profile['active_name']}), "
                    f"requested={profile['requested_name']}, saved={profile['saved_profile_name']}, "
                    f"persisted={profile['persisted']}, two_ctrl={profile['two_controller_mode']}, "
                    f"codec={profile['codec_name']}, transition={profile['transition']}, "
                    f"failure={profile['failure']}, profile_mask=0x{profile['profile_mask']:02x}, "
                    f"codec_mask=0x{profile['codec_mask']:02x}"
                )
            if args.set_profile:
                next_state = set_profile(
                    port,
                    PROFILE_IDS[args.set_profile],
                    CODEC_IDS[args.codec],
                    persist=args.persist,
                    sequence=102,
                )
                print(
                    f"Profile switch requested: active={next_state['active_name']}, "
                    f"requested={next_state['requested_name']}, persisted={next_state['persisted']}, "
                    f"transition={next_state['transition']}"
                )
            if args.mic_config or args.set_mic_source:
                if args.set_mic_source:
                    mic = set_mic_source(port, MIC_SOURCE_IDS[args.set_mic_source], sequence=107)
                else:
                    mic = read_mic_state(port, sequence=106)
                if mic is None:
                    print("Mic config: not supported by running firmware (legacy)")
                else:
                    print(
                        f"Mic state: configured={mic['configured_source_name']}, active={mic['active_source_name']}, "
                        f"codec={mic['active_codec_name']}, usb_open={mic['usb_open']}, "
                        f"usb_muted={mic['usb_muted']}, hfp_slc={mic['hfp_slc_ready']}, "
                        f"hfp_sco={mic['hfp_sco_open']}, sco_cb={mic['sco_callback_seen']}, "
                        f"peak={mic['mic_peak']}, decoded={mic['decoded_frames']}, errors={mic['error_frames']}"
                    )
            if args.remove_audio:
                if remove_audio(port, sequence=108) is None:
                    print("Remove audio: not supported by running firmware (legacy)")
                else:
                    print("Removed paired Bluetooth headphone record")
            if args.block:
                bl = block_device(port, parse_mac_address(args.block), sequence=110)
                print(f"Blocklist updated ({len(bl)}): {', '.join(bl)}")
            if args.unblock:
                bl = unblock_device(port, parse_mac_address(args.unblock), sequence=111)
                print(f"Blocklist updated ({len(bl)}): {', '.join(bl)}")
            if args.blocklist:
                bl = read_blocklist(port, sequence=109)
                if bl is None:
                    print("Blocklist: not supported by running firmware (legacy)")
                else:
                    print(f"Blocklist ({len(bl)}): {', '.join(bl) if bl else '(empty)'}")
            if args.dualsense_metrics is not None:
                metrics = read_dualsense_metrics(port, slot=args.dualsense_metrics, sequence=103)
                if metrics is None:
                    print("DualSense metrics: not supported by running firmware (legacy)")
                else:
                    print(
                        f"DualSense metrics slot {metrics['slot']}: conn_flags=0x{metrics['conn_flags']:02x}, "
                        f"feat_mask=0x{metrics['feat_mask']:02x}, bt_in_31={metrics['bt_in_31']}, "
                        f"bt_in_01={metrics['bt_in_01']}, usb_out_02={metrics['usb_out_02']}, "
                        f"bt_out_31_ok={metrics['bt_out_31_ok']}, bt_out_31_err={metrics['bt_out_31_err']}, "
                        f"bt_feat_set_ok={metrics['bt_feat_set_ok']}, bt_feat_get_ok={metrics['bt_feat_get_ok']}, "
                        f"last_feat=0x{metrics['last_feat_id']:02x}, last_hs=0x{metrics['last_handshake']:02x}, "
                        f"path={metrics['last_out_path_flags']}"
                        + (
                            f", flag0=0x{metrics['last_valid_flag0']:02x}, "
                            f"flag1=0x{metrics['last_valid_flag1']:02x}, "
                            f"motors=({metrics['last_motor_left']},{metrics['last_motor_right']}), "
                            f"trig=(0x{metrics['last_left_trigger_mode']:02x},"
                            f"0x{metrics['last_right_trigger_mode']:02x}), "
                            f"fw_lo=0x{metrics['fw_ver_lo']:02x}"
                            if metrics["fw_ver_lo"] is not None
                            else ""
                        )
                    )
            if args.dualsense_passthrough_test:
                run_dualsense_passthrough_test(port)
            if image is not None:
                print(f"Uploading {len(image)} bytes through HID feature reports", flush=True)
                usb_ota.upload(port, image, retries=0, timeout=10)
                print("HID OTA accepted; wait for reboot and rerun checks")
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"HID check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
