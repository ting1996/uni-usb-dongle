import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import usb_audio_check


def _build_sbc_rtp_packet(frame_prefix_hex: str = "9cbd35a8809000000000000000", frame_count: int = 5) -> bytes:
    prefix = bytes.fromhex(frame_prefix_hex)
    frame = prefix + bytes(119 - len(prefix))
    rtp_header = struct.pack(">BBHII", 0x80, 96, 42, 1280, 0x11223344)
    return rtp_header + bytes((frame_count,)) + frame * frame_count


class UsbAudioCheckTests(unittest.TestCase):
    def test_vid_pid_tags_cover_all_output_profiles(self):
        tags = usb_audio_check._dongle_vid_pid_tags()
        self.assertEqual(
            tags,
            (
                "vid_ffff&pid_ffff",
                "vid_045e&pid_028e",
                "vid_054c&pid_09cc",
                "vid_057e&pid_2009",
                "vid_054c&pid_0ce6",
            ),
        )
        self.assertEqual(usb_audio_check._dongle_vid_pid_tags(0x054C, 0x09CC), ("vid_054c&pid_09cc",))

    def test_analyze_valid_sbc_rtp_packet_and_energy(self):
        packet = _build_sbc_rtp_packet("9cbd35a8809000000000000000", frame_count=5)
        meta = usb_audio_check.analyze_sbc_rtp_packet(packet)
        self.assertEqual(meta["rtp_seq"], 42)
        self.assertEqual(meta["frame_count"], 5)
        self.assertEqual(meta["sample_rate_hz"], 44100)
        self.assertEqual(meta["blocks"], 16)
        self.assertEqual(meta["channel_mode"], "joint_stereo")
        self.assertEqual(meta["allocation"], "loudness")
        self.assertEqual(meta["subbands"], 8)
        self.assertEqual(meta["bitpool"], 53)
        self.assertEqual(meta["frame_bytes"], 119)
        self.assertEqual(meta["max_scale_factor"], 9)
        self.assertTrue(meta["has_audio_energy"])

        tone_packet = _build_sbc_rtp_packet("9cbd35ccfc9431110000000000", frame_count=5)
        tone_meta = usb_audio_check.analyze_sbc_rtp_packet(tone_packet)
        self.assertEqual(tone_meta["max_scale_factor"], 9)
        self.assertTrue(tone_meta["has_audio_energy"])

    def test_analyze_sbc_rtp_rejects_bad_crc_truncated_and_fragmented(self):
        bad_crc = _build_sbc_rtp_packet("9cbd35a9809000000000000000", frame_count=5)
        with self.assertRaisesRegex(ValueError, "CRC-8 mismatch"):
            usb_audio_check.analyze_sbc_rtp_packet(bad_crc)

        valid = _build_sbc_rtp_packet("9cbd35a8809000000000000000", frame_count=5)
        with self.assertRaisesRegex(ValueError, "truncated"):
            usb_audio_check.analyze_sbc_rtp_packet(valid[:-1])

        fragmented = bytearray(valid)
        fragmented[12] = 0x80
        with self.assertRaisesRegex(ValueError, "fragmented"):
            usb_audio_check.analyze_sbc_rtp_packet(bytes(fragmented))

    def test_wavein_and_mic_helpers_present_and_match_dongle(self):
        from unittest import mock

        self.assertTrue(callable(usb_audio_check.find_windows_wavein_device))
        self.assertTrue(callable(usb_audio_check.record_pcm_windows))
        with mock.patch.object(
            usb_audio_check, "_find_windows_wave_device", return_value=(1, "Microphone (Bluetooth Gamepads)")
        ) as patched:
            dev = usb_audio_check.find_windows_wavein_device(0x054C, 0x0CE6)
            self.assertEqual(dev, (1, "Microphone (Bluetooth Gamepads)"))
            patched.assert_called_once_with(True, vid=0x054C, pid=0x0CE6)


if __name__ == "__main__":
    unittest.main()
