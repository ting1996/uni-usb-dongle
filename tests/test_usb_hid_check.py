import unittest
from tools import usb_hid_check, usb_ota


class HidCheckTests(unittest.TestCase):
    def test_fragments_match_the_webhid_control_report_format(self):
        packet = usb_ota.encode_frame(2, 512, bytes(range(256)) * 4)
        reports = list(usb_hid_check.feature_fragments(packet))
        self.assertEqual(len(reports), 18)
        data = bytearray()
        for report in reports:
            self.assertEqual(len(report), 64)
            self.assertEqual(report[0], 2)
            self.assertEqual(int.from_bytes(report[1:3], "little"), len(data))
            data.extend(report[4:4 + report[3]])
        self.assertEqual(data, packet)

    def test_response_checksum_and_report_metadata_are_validated(self):
        packet = usb_ota.encode_frame(0x90, 1, b"\x00\x02")
        response = bytearray(64)
        response[0] = 2
        response[3] = len(packet)
        response[4:4 + len(packet)] = packet
        self.assertEqual(usb_hid_check.decode_feature(response), packet)
        for index, value in ((0, 3), (1, 1), (3, 61), (19, 99)):
            bad = bytearray(response)
            bad[index] = value
            with self.assertRaises(ValueError):
                usb_hid_check.decode_feature(bad)

    def test_windows_management_hid_selection_filters_vendor_collection(self):
        devices = [
            {"path": r"\\?\hid#vid_ffff&pid_ffff&mi_02&col01#a#guid", "usage_page": 0x0001, "usage": 0x0005, "feature_len": 0},
            {"path": r"\\?\hid#vid_ffff&pid_ffff&mi_02&col02#a#guid", "usage_page": 0xFF00, "usage": 0x0001, "feature_len": 64},
            {"path": r"\\?\hid#vid_ffff&pid_ffff&mi_03#a#guid", "usage_page": 0x0001, "usage": 0x0005, "feature_len": 0},
        ]
        self.assertEqual(
            usb_hid_check._resolve_windows_mgmt_path("auto", devices=devices),
            r"\\?\hid#vid_ffff&pid_ffff&mi_02&col02#a#guid",
        )
        self.assertEqual(
            usb_hid_check._resolve_windows_mgmt_path("/dev/hidraw0", devices=devices),
            r"\\?\hid#vid_ffff&pid_ffff&mi_02&col02#a#guid",
        )
        with self.assertRaisesRegex(OSError, "no uni-usb-dongle Windows HID"):
            usb_hid_check._resolve_windows_mgmt_path("auto", devices=devices[:1])
        with self.assertRaisesRegex(OSError, "multiple"):
            usb_hid_check._resolve_windows_mgmt_path("auto", devices=[devices[1], devices[1]])

    def test_windows_management_hid_selection_accepts_all_profile_vid_pids(self):
        self.assertEqual(
            set(usb_hid_check.DONGLE_VID_PIDS),
            {(0xFFFF, 0xFFFF), (0x045E, 0x028E), (0x054C, 0x09CC), (0x057E, 0x2009), (0x054C, 0x0CE6)},
        )
        for vid_pid in ("vid_ffff&pid_ffff", "vid_045e&pid_028e", "vid_054c&pid_09cc", "vid_057e&pid_2009", "vid_054c&pid_0ce6"):
            devices = [
                {"path": rf"\\?\hid#{vid_pid}&mi_03#a#guid", "usage_page": 0x0001, "usage": 0x0005, "feature_len": 0},
                {"path": rf"\\?\hid#{vid_pid}&mi_02#a#guid", "usage_page": 0xFF00, "usage": 0x0001, "feature_len": 64},
            ]
            self.assertEqual(
                usb_hid_check._resolve_windows_mgmt_path("auto", devices=devices),
                rf"\\?\hid#{vid_pid}&mi_02#a#guid",
            )

    def test_decode_profile_state_and_reject_invalid(self):
        payload = bytes([0, 1, 2, 2, 0x0F, 1, 1, 0x0F, 0, 0, 0, 0])
        decoded = usb_hid_check.decode_profile_state(payload)
        self.assertEqual(decoded["active"], 2)
        self.assertEqual(decoded["active_name"], "ps4")
        self.assertEqual(decoded["requested_name"], "ps4")
        self.assertEqual(decoded["codec_name"], "sbc")
        self.assertEqual(decoded["streaming"], 1)
        self.assertEqual(decoded["profile_mask"], 0x0F)
        self.assertEqual(decoded["codec_mask"], 0x0F)

        idle_payload = bytes([0, 1, 0, 0, 0x0F, 0, 0xFF, 0x0F, 0, 0, 0, 0])
        self.assertIsNone(usb_hid_check.decode_profile_state(idle_payload)["streaming"])

        ds_payload = bytes([0, 1, 4, 4, 0x1F, 1, 1, 0x03, 0, 0, 0, 0])
        ds_decoded = usb_hid_check.decode_profile_state(ds_payload)
        self.assertEqual(ds_decoded["active"], 4)
        self.assertEqual(ds_decoded["active_name"], "dualsense")
        self.assertEqual(ds_decoded["profile_mask"], 0x1F)

        with self.assertRaises(ValueError):
            usb_hid_check.decode_profile_state(b"\x02")
        with self.assertRaises(ValueError):
            usb_hid_check.decode_profile_state(b"\x01" + bytes(11))

    def test_read_profile_state_returns_none_for_legacy_firmware_status(self):
        from unittest import mock

        with mock.patch.object(usb_hid_check.usb_device_check, "_request", return_value=b"\x02\x00\x00\x00\x00"):
            self.assertIsNone(usb_hid_check.read_profile_state(None))
        with mock.patch.object(usb_hid_check.usb_device_check, "_request", return_value=b"\x08"):
            self.assertIsNone(usb_hid_check.read_profile_state(None))

    def test_decode_and_read_dualsense_metrics(self):
        from unittest import mock

        payload = bytearray(30)
        payload[0] = 0
        payload[1] = 0
        payload[2] = 0x3F
        payload[3] = 0x37
        import struct
        struct.pack_into("<5I2H", payload, 4, 1200, 3, 45, 44, 1, 2, 11)
        payload[28] = 0x81
        payload[29] = 0x00

        decoded = usb_hid_check.decode_dualsense_metrics(bytes(payload))
        self.assertEqual(decoded["slot"], 0)
        self.assertEqual(decoded["conn_flags"], 0x3F)
        self.assertEqual(decoded["feat_mask"], 0x37)
        self.assertEqual(decoded["bt_in_31"], 1200)
        self.assertEqual(decoded["bt_in_01"], 3)
        self.assertEqual(decoded["usb_out_02"], 45)
        self.assertEqual(decoded["bt_out_31_ok"], 44)
        self.assertEqual(decoded["bt_out_31_err"], 1)
        self.assertEqual(decoded["bt_feat_set_ok"], 2)
        self.assertEqual(decoded["bt_feat_get_ok"], 11)
        self.assertEqual(decoded["last_feat_id"], 0x81)
        self.assertEqual(decoded["last_handshake"], 0x00)

        with self.assertRaises(ValueError):
            usb_hid_check.decode_dualsense_metrics(b"\x00" * 29)
        with self.assertRaises(ValueError):
            usb_hid_check.decode_dualsense_metrics(b"\x01" + bytes(29))
        with mock.patch.object(usb_hid_check.usb_device_check, "_request", return_value=b"\x02"):
            self.assertIsNone(usb_hid_check.read_dualsense_metrics(None))

        payload_v11 = bytearray(payload) + bytearray([0x03, 0x0C, 0x7F, 0x40, 0x01, 0x22, 0x05, 0x02])
        decoded_v11 = usb_hid_check.decode_dualsense_metrics(bytes(payload_v11))
        self.assertEqual(decoded_v11["last_valid_flag0"], 0x03)
        self.assertEqual(decoded_v11["last_valid_flag1"], 0x0C)
        self.assertEqual(decoded_v11["last_motor_right"], 0x7F)
        self.assertEqual(decoded_v11["last_motor_left"], 0x40)
        self.assertEqual(decoded_v11["last_right_trigger_mode"], 0x01)
        self.assertEqual(decoded_v11["last_left_trigger_mode"], 0x22)
        self.assertEqual(decoded_v11["last_out_path_flags"], 0x05)
        self.assertEqual(decoded_v11["fw_ver_lo"], 0x02)

    def test_decode_mic_state_blocklist_and_profile_v11(self):
        import struct

        prof = usb_hid_check.decode_profile_state(bytes([0, 1, 4, 4, 0x1F, 1, 0xFF, 3, 0, 0, 4, 0x03]))
        self.assertEqual(prof["saved_profile"], 4)
        self.assertEqual(prof["saved_profile_name"], "dualsense")
        self.assertTrue(prof["persisted"])
        self.assertTrue(prof["two_controller_mode"])

        mic_bytes = bytearray(16)
        mic_bytes[1] = 2
        mic_bytes[2] = 2
        mic_bytes[3] = 3
        mic_bytes[4] = 0x13
        struct.pack_into("<HII", mic_bytes, 6, 9876, 400, 1)
        mic = usb_hid_check.decode_mic_state(bytes(mic_bytes))
        self.assertEqual(mic["configured_source_name"], "controller0")
        self.assertEqual(mic["active_codec_name"], "opus")
        self.assertTrue(mic["usb_open"])
        self.assertTrue(mic["usb_muted"])
        self.assertTrue(mic["ds0_connected"])
        self.assertEqual(mic["mic_peak"], 9876)
        self.assertEqual(mic["decoded_frames"], 400)
        self.assertEqual(mic["error_frames"], 1)

        bl = usb_hid_check.decode_blocklist(bytes([0, 2, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]))
        self.assertEqual(bl, ["11:22:33:44:55:66", "AA:BB:CC:DD:EE:FF"])
        with self.assertRaises(ValueError):
            usb_hid_check.decode_blocklist(bytes([0, 1, 6, 0x11]))


if __name__ == "__main__":
    unittest.main()
