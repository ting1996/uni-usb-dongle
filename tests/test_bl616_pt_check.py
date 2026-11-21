import io
import json
import struct
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import bl616_pt_check


def _build_bfpt_table(age: int, fw_active_index: int) -> bytes:
    entries = [
        (7, 0, 0, "Boot2", 0x00000000, 0x00000000, 0x0000E000, 0x00000000, 0x0000E000, 0),
        (0, 0, fw_active_index, "FW", 0x00010000, 0x00210000, 0x00200000, 0x00138000, 0x000D0000, age),
        (2, 0, 0, "mfg", 0x00210000, 0x00000000, 0x00040000, 0x00000000, 0x00000000, 0),
        (2, 0, 0, "media", 0x00250000, 0x00000000, 0x000F8000, 0x00000000, 0x00000000, 0),
        (3, 0, 0, "PSM", 0x00348000, 0x00000000, 0x00040000, 0x00000000, 0x00000000, 0),
        (4, 0, 0, "KEY", 0x00388000, 0x00000000, 0x00002000, 0x00000000, 0x00000000, 0),
        (5, 0, 0, "DATA", 0x0038A000, 0x00000000, 0x00005000, 0x00000000, 0x00000000, 0),
        (6, 0, 0, "factory", 0x003F0000, 0x00000000, 0x00010000, 0x00000000, 0x00000000, 0),
    ]
    entry_bytes = bytearray()
    for e_type, dev, idx, name, a0, a1, m0, m1, length, e_age in entries:
        raw_name = name.encode("ascii").ljust(9, b"\x00")[:9]
        entry_bytes += struct.pack(
            bl616_pt_check.PT_ENTRY_FORMAT,
            e_type,
            dev,
            idx,
            raw_name,
            a0,
            a1,
            m0,
            m1,
            length,
            e_age,
        )

    hdr_wo_crc = struct.pack("<IHHI", bl616_pt_check.PT_MAGIC, 1, len(entries), age)
    hdr_crc = zlib.crc32(hdr_wo_crc) & 0xFFFFFFFF
    entries_crc = zlib.crc32(entry_bytes) & 0xFFFFFFFF
    return hdr_wo_crc + struct.pack("<I", hdr_crc) + bytes(entry_bytes) + struct.pack("<I", entries_crc)


def _build_dual_8k_dump(age0: int = 4, idx0: int = 0, age1: int = 5, idx1: int = 1) -> bytearray:
    buf = bytearray(b"\xff" * 0x2000)
    t0 = _build_bfpt_table(age=age0, fw_active_index=idx0)
    t1 = _build_bfpt_table(age=age1, fw_active_index=idx1)
    buf[0 : len(t0)] = t0
    buf[0x1000 : 0x1000 + len(t1)] = t1
    return buf


class Bl616PtCheckTests(unittest.TestCase):
    def test_inspect_dual_copy_selects_higher_age_and_active_slot(self):
        dump = bytes(_build_dual_8k_dump(age0=4, idx0=0, age1=5, idx1=1))
        summary = bl616_pt_check.inspect_dump(dump)
        self.assertEqual(summary["base_offset"], 0xE000)
        self.assertEqual(summary["active_table_id"], 1)
        self.assertEqual(summary["active_fw"]["active_index"], 1)
        self.assertEqual(summary["active_fw"]["active_address"], 0x00210000)
        self.assertEqual(
            [e["name"] for e in summary["active_table"]["entries"]],
            ["Boot2", "FW", "mfg", "media", "PSM", "KEY", "DATA", "factory"],
        )

    def test_inspect_allows_erased_secondary_copy(self):
        dump = bytearray(b"\xff" * 0x2000)
        t0 = _build_bfpt_table(age=2, fw_active_index=0)
        dump[0 : len(t0)] = t0
        summary = bl616_pt_check.inspect_dump(bytes(dump))
        self.assertEqual(summary["active_table_id"], 0)
        self.assertEqual(summary["tables"]["table1"]["status"], "erased")
        self.assertEqual(summary["active_fw"]["active_address"], 0x00010000)

    def test_corrupted_header_or_entry_crc_raises(self):
        bad_hdr = _build_dual_8k_dump()
        bad_hdr[12] ^= 0x01  # Flip bit in table0 header CRC
        with self.assertRaisesRegex(ValueError, "header CRC-32 mismatch"):
            bl616_pt_check.inspect_dump(bytes(bad_hdr))

        bad_entries = _build_dual_8k_dump()
        bad_entries[0x1000 + 24] ^= 0x80  # Flip bit in table1 first entry
        with self.assertRaisesRegex(ValueError, "entries CRC-32 mismatch"):
            bl616_pt_check.inspect_dump(bytes(bad_entries))

        with self.assertRaisesRegex(ValueError, "No valid BL616 partition table"):
            bl616_pt_check.inspect_dump(b"\xff" * 0x2000)

    def test_cli_json_output(self):
        dump = bytes(_build_dual_8k_dump(age0=10, idx0=0, age1=9, idx1=1))
        with tempfile.TemporaryDirectory() as tmpdir:
            bin_path = Path(tmpdir) / "pt.bin"
            bin_path.write_bytes(dump)
            buf = io.StringIO()
            with redirect_stdout(buf):
                rc = bl616_pt_check.main([str(bin_path), "--offset", "0xe000", "--json"])
            self.assertEqual(rc, 0)
            payload = json.loads(buf.getvalue())
            self.assertTrue(payload["ok"])
            self.assertEqual(payload["active_table_id"], 0)
            self.assertEqual(payload["active_fw"]["active_address"], 0x00010000)


if __name__ == "__main__":
    unittest.main()
