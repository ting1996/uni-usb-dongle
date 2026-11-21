import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import usb_gamepad_check


class UsbGamepadCheckTests(unittest.TestCase):
    def test_rejects_the_measured_slow_rtos_clock(self):
        with patch.object(usb_gamepad_check, "read_diagnostics",
                          side_effect=((0, 0, 100, 0), (0, 0, 101, 0))), \
                patch.object(usb_gamepad_check.time, "sleep"), \
                patch.object(usb_gamepad_check.time, "monotonic",
                             side_effect=(0, 0.25)):
            with self.assertRaisesRegex(ValueError, "RTOS clock is 4.0"):
                usb_gamepad_check.check_tick_rate(object(), 1)

    def test_clock_check_handles_tick_wraparound(self):
        with patch.object(usb_gamepad_check, "read_diagnostics", side_effect=(
                (0, 0, 0xffffff80, 0), (0, 0, 122, 0))), \
                patch.object(usb_gamepad_check.time, "sleep"), \
                patch.object(usb_gamepad_check.time, "monotonic",
                             side_effect=(0, 0.25)), patch("builtins.print"):
            usb_gamepad_check.check_tick_rate(object(), 1)

    def test_calibration_matches_observed_linux_gamepad_events(self):
        axes = (112, 142, 5534751, 5534751, 0, 0, 0, 0, 0, 1)
        hat = (0, 0, 536870912, 536870912, 0, 0, 0, 0, 0, 1)
        corrections = [axes] * 6 + [hat] * 2
        actual, buttons = usb_gamepad_check.joystick_expected(
            usb_gamepad_check.REPORTS[0], corrections)
        self.assertEqual(actual, [-27026, 26349, -16216, -7432, 6080, 0, 32767, 0])
        self.assertEqual(buttons, [1] + [0] * 15)
        neutral, buttons = usb_gamepad_check.joystick_expected(
            usb_gamepad_check.NEUTRAL, corrections)
        self.assertEqual(neutral, [0, 0, -32767, 0, 0, -32767, 0, 0])
        self.assertEqual(buttons, [0] * 16)

    def test_selects_usb_parent_of_the_requested_serial_port(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            device = root / "usb1" / "1-2"
            intf_name = "1-2_1.0" if os.name == "nt" else "1-2:1.0"
            intf = device / intf_name
            intf.mkdir(parents=True)
            (device / "idVendor").write_text("ffff")
            (device / "busnum").write_text("1")
            (device / "devnum").write_text("17")
            tty = root / "tty" / "ttyACM0"
            tty.mkdir(parents=True)
            try:
                (tty / "device").symlink_to(intf, target_is_directory=True)
            except (OSError, NotImplementedError):
                # Windows without developer-mode/SeCreateSymbolicLinkPrivilege
                # cannot create symlinks; verify parent traversal directly.
                device = tty
                (tty / "device" / intf_name).mkdir(parents=True)
                (device / "idVendor").write_text("ffff")
                (device / "busnum").write_text("1")
                (device / "devnum").write_text("17")
            self.assertEqual(usb_gamepad_check.usb_device_path(
                "/dev/ttyACM0", root / "tty"), device)

    def test_missing_usb_identity_is_an_error(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaisesRegex(ValueError, "USB identity"):
                usb_gamepad_check.usb_device_path("/dev/missing", Path(root))

    def test_diagnostic_ack_is_checked(self):
        with patch.object(usb_gamepad_check.usb_device_check, "_request",
                          return_value=bytes((3, 0, 0, 0, 0))):
            with self.assertRaisesRegex(ValueError, "test report rejected"):
                usb_gamepad_check.request_report(
                    object(), 0, usb_gamepad_check.NEUTRAL, 1, 1)

    def test_windows_gamepad_matcher_maps_mi_02_and_mi_03_ignoring_mgmt(self):
        devices = [
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_02&col02#a#guid",
                "usage_page": 0xFF00, "usage": 0x0001, "input_len": 0,
                "input_button_caps": 0, "input_value_caps": 0,
            },
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_03#a#guid",
                "usage_page": 0x0001, "usage": 0x0005, "input_len": 10,
                "input_button_caps": 1, "input_value_caps": 7,
            },
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_02&col01#a#guid",
                "usage_page": 0x0001, "usage": 0x0005, "input_len": 10,
                "input_button_caps": 1, "input_value_caps": 7,
            },
        ]
        nodes = usb_gamepad_check._match_windows_gamepads(devices)
        self.assertEqual(nodes, {
            0: r"\\?\hid#vid_ffff&pid_ffff&mi_02&col01#a#guid",
            1: r"\\?\hid#vid_ffff&pid_ffff&mi_03#a#guid",
        })
        with self.assertRaisesRegex(ValueError, "MI_02 and MI_03"):
            usb_gamepad_check._match_windows_gamepads(devices[:2])

    def test_windows_gamepad_matcher_maps_mi_03_and_mi_04_ignoring_mgmt(self):
        devices = [
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_02#a#guid",
                "usage_page": 0xFF00, "usage": 0x0001, "input_len": 0,
                "input_button_caps": 0, "input_value_caps": 0,
            },
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_03#a#guid",
                "usage_page": 0x0001, "usage": 0x0005, "input_len": 10,
                "input_button_caps": 1, "input_value_caps": 7,
            },
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_04#a#guid",
                "usage_page": 0x0001, "usage": 0x0005, "input_len": 10,
                "input_button_caps": 1, "input_value_caps": 7,
            },
        ]
        nodes = usb_gamepad_check._match_windows_gamepads(devices)
        self.assertEqual(nodes, {
            0: r"\\?\hid#vid_ffff&pid_ffff&mi_03#a#guid",
            1: r"\\?\hid#vid_ffff&pid_ffff&mi_04#a#guid",
        })

    def test_windows_gamepad_matcher_maps_mi_04_and_mi_05_ignoring_mgmt(self):
        devices = [
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_03#a#guid",
                "usage_page": 0xFF00, "usage": 0x0001, "input_len": 0,
                "input_button_caps": 0, "input_value_caps": 0,
            },
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_04#a#guid",
                "usage_page": 0x0001, "usage": 0x0005, "input_len": 10,
                "input_button_caps": 1, "input_value_caps": 7,
            },
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_05#a#guid",
                "usage_page": 0x0001, "usage": 0x0005, "input_len": 10,
                "input_button_caps": 1, "input_value_caps": 7,
            },
        ]
        nodes = usb_gamepad_check._match_windows_gamepads(devices)
        self.assertEqual(nodes, {
            0: r"\\?\hid#vid_ffff&pid_ffff&mi_04#a#guid",
            1: r"\\?\hid#vid_ffff&pid_ffff&mi_05#a#guid",
        })

    def test_windows_gamepad_matcher_maps_single_mi_04_when_one_controller_exposed(self):
        devices = [
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_03#a#guid",
                "usage_page": 0xFF00, "usage": 0x0001, "input_len": 0,
                "input_button_caps": 0, "input_value_caps": 0,
            },
            {
                "path": r"\\?\hid#vid_ffff&pid_ffff&mi_04#a#guid",
                "usage_page": 0x0001, "usage": 0x0005, "input_len": 10,
                "input_button_caps": 1, "input_value_caps": 7,
            },
        ]
        nodes = usb_gamepad_check._match_windows_gamepads(devices)
        self.assertEqual(nodes, {
            0: r"\\?\hid#vid_ffff&pid_ffff&mi_04#a#guid",
        })



if __name__ == "__main__":
    unittest.main()
