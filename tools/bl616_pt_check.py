#!/usr/bin/env python3
"""Parse and verify Bouffalo BL616 partition table dumps (BFPT format)."""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys
import zlib
from typing import Any

PT_MAGIC = 0x54504642  # "BFPT" in little-endian
PT_MAX_ENTRIES = 16
PT_HEADER_FORMAT = "<IHHII"
PT_HEADER_SIZE = struct.calcsize(PT_HEADER_FORMAT)  # 16 bytes
PT_ENTRY_FORMAT = "<BBB9s6I"
PT_ENTRY_SIZE = struct.calcsize(PT_ENTRY_FORMAT)  # 36 bytes
PT_CRC_SIZE = 4

PT_TABLE_0_FLASH_OFFSET = 0x0000E000
PT_TABLE_1_FLASH_OFFSET = 0x0000F000
PT_REGION_SIZE = 0x00002000

KNOWN_PARTITION_DESCRIPTIONS = {
    "Boot2": "Boot2 bootloader",
    "FW": "Application firmware (dual-slot OTA)",
    "mfg": "Manufacturing / RF factory data",
    "media": "Media / assets",
    "PSM": "Persistent storage module (EasyFlash)",
    "KEY": "Cryptographic keys",
    "DATA": "Application data",
    "factory": "Factory calibration parameters",
}


def _decode_name(raw_name: bytes) -> str:
    nul = raw_name.find(b"\x00")
    if nul >= 0:
        raw_name = raw_name[:nul]
    return raw_name.decode("ascii", errors="replace")


def parse_table(blob: bytes, offset: int = 0) -> dict[str, Any]:
    """Parse and CRC-verify a single BL616 BFPT partition table at `offset`."""
    if offset < 0:
        raise ValueError(f"Negative table offset: {offset}")
    if offset + PT_HEADER_SIZE > len(blob):
        raise ValueError(
            f"Truncated partition table header at offset 0x{offset:x} "
            f"(need {PT_HEADER_SIZE} bytes, have {max(0, len(blob) - offset)})"
        )

    magic, version, entry_cnt, age, header_crc = struct.unpack_from(PT_HEADER_FORMAT, blob, offset)
    if magic != PT_MAGIC:
        raise ValueError(
            f"Invalid partition table magic at offset 0x{offset:x}: "
            f"0x{magic:08x} (expected 0x{PT_MAGIC:08x})"
        )
    if entry_cnt > PT_MAX_ENTRIES:
        raise ValueError(
            f"Invalid partition entry count {entry_cnt} at offset 0x{offset:x} "
            f"(max {PT_MAX_ENTRIES})"
        )

    calc_header_crc = zlib.crc32(blob[offset : offset + PT_HEADER_SIZE - PT_CRC_SIZE]) & 0xFFFFFFFF
    if calc_header_crc != header_crc:
        raise ValueError(
            f"Partition table header CRC-32 mismatch at offset 0x{offset:x}: "
            f"stored=0x{header_crc:08x}, calculated=0x{calc_header_crc:08x}"
        )

    entries_start = offset + PT_HEADER_SIZE
    entries_size = entry_cnt * PT_ENTRY_SIZE
    entries_end = entries_start + entries_size
    if entries_end + PT_CRC_SIZE > len(blob):
        raise ValueError(
            f"Truncated partition entries at offset 0x{offset:x} "
            f"(need {entries_size + PT_CRC_SIZE} bytes, have {max(0, len(blob) - entries_start)})"
        )

    entries_bytes = blob[entries_start:entries_end]
    stored_entries_crc = struct.unpack_from("<I", blob, entries_end)[0]
    calc_entries_crc = zlib.crc32(entries_bytes) & 0xFFFFFFFF
    if calc_entries_crc != stored_entries_crc:
        raise ValueError(
            f"Partition table entries CRC-32 mismatch at offset 0x{offset:x}: "
            f"stored=0x{stored_entries_crc:08x}, calculated=0x{calc_entries_crc:08x}"
        )

    entries: list[dict[str, Any]] = []
    for index in range(entry_cnt):
        entry_off = entries_start + index * PT_ENTRY_SIZE
        (
            entry_type,
            device,
            active_index,
            raw_name,
            addr0,
            addr1,
            max0,
            max1,
            length,
            entry_age,
        ) = struct.unpack_from(PT_ENTRY_FORMAT, blob, entry_off)
        name = _decode_name(raw_name)
        slot_index = 1 if active_index == 1 and (addr1 != 0 or max1 != 0) else 0
        entries.append(
            {
                "index": index,
                "name": name,
                "description": KNOWN_PARTITION_DESCRIPTIONS.get(name, ""),
                "type": entry_type,
                "device": device,
                "active_index": active_index,
                "start_address": [addr0, addr1],
                "max_len": [max0, max1],
                "active_address": addr1 if slot_index == 1 else addr0,
                "active_max_len": max1 if slot_index == 1 else max0,
                "length": length,
                "age": entry_age & 0x00FFFFFF,
                "boot_retry": (entry_age >> 24) & 0xFF,
                "age_raw": entry_age,
            }
        )

    return {
        "offset": offset,
        "magic": f"0x{magic:08x}",
        "version": version,
        "entry_cnt": entry_cnt,
        "age": age,
        "header_crc32": f"0x{header_crc:08x}",
        "entries_crc32": f"0x{stored_entries_crc:08x}",
        "entries": entries,
    }


def _infer_base_offset(data: bytes, base_offset: int | None) -> int:
    if base_offset is not None:
        if base_offset < 0:
            raise ValueError(f"Negative base_offset: {base_offset}")
        return base_offset
    if len(data) >= PT_TABLE_1_FLASH_OFFSET + PT_HEADER_SIZE:
        return 0
    return PT_TABLE_0_FLASH_OFFSET


def inspect_dump(data: bytes, base_offset: int | None = None) -> dict[str, Any]:
    """Inspect a flash or partition-table-region dump and verify dual BFPT copies."""
    resolved_base = _infer_base_offset(data, base_offset)

    if resolved_base > PT_TABLE_0_FLASH_OFFSET:
        # Caller specified an arbitrary offset directly inside `data`.
        table = parse_table(data, offset=resolved_base)
        table["flash_offset"] = resolved_base
        active_fw = _extract_active_fw(table["entries"])
        return {
            "base_offset": 0,
            "active_table_id": 0,
            "active_table": table,
            "tables": {"table0": table, "table1": None},
            "active_fw": active_fw,
        }

    candidate_slots = [
        ("table0", 0, PT_TABLE_0_FLASH_OFFSET - resolved_base, PT_TABLE_0_FLASH_OFFSET),
        ("table1", 1, PT_TABLE_1_FLASH_OFFSET - resolved_base, PT_TABLE_1_FLASH_OFFSET),
    ]

    parsed_tables: dict[str, dict[str, Any] | None] = {"table0": None, "table1": None}
    valid_copies: list[tuple[int, dict[str, Any]]] = []

    for label, table_id, buf_off, flash_off in candidate_slots:
        if buf_off < 0 or buf_off >= len(data):
            continue
        if len(data) - buf_off < PT_HEADER_SIZE:
            raise ValueError(
                f"Truncated {label} header at buffer offset 0x{buf_off:x} "
                f"(flash 0x{flash_off:08x})"
            )
        if data[buf_off : buf_off + PT_HEADER_SIZE] == b"\xff" * PT_HEADER_SIZE:
            parsed_tables[label] = {"status": "erased", "offset": buf_off, "flash_offset": flash_off}
            continue

        table = parse_table(data, offset=buf_off)
        table["table_id"] = table_id
        table["flash_offset"] = flash_off
        table["status"] = "valid"
        parsed_tables[label] = table
        valid_copies.append((table_id, table))

    if not valid_copies:
        raise ValueError(
            f"No valid BL616 partition table found (checked base_offset=0x{resolved_base:x}, "
            f"dump_size={len(data)} bytes)"
        )

    # Prefer the valid table with the highest age; break ties in favor of table 0.
    active_id, active_table = max(valid_copies, key=lambda item: (item[1]["age"], -item[0]))
    active_fw = _extract_active_fw(active_table["entries"])

    return {
        "base_offset": resolved_base,
        "active_table_id": active_id,
        "active_table": active_table,
        "tables": parsed_tables,
        "active_fw": active_fw,
    }


def _extract_active_fw(entries: list[dict[str, Any]]) -> dict[str, Any] | None:
    fw_entry = next((entry for entry in entries if entry["name"] == "FW"), None)
    if fw_entry is None:
        fw_entry = next((entry for entry in entries if entry["type"] == 0), None)
    if fw_entry is None:
        return None
    return {
        "name": fw_entry["name"],
        "active_index": fw_entry["active_index"],
        "active_address": fw_entry["active_address"],
        "active_max_len": fw_entry["active_max_len"],
        "slot0_address": fw_entry["start_address"][0],
        "slot0_max_len": fw_entry["max_len"][0],
        "slot1_address": fw_entry["start_address"][1],
        "slot1_max_len": fw_entry["max_len"][1],
        "length": fw_entry["length"],
        "age": fw_entry["age"],
    }


def format_report(summary: dict[str, Any]) -> str:
    """Format a human-readable report for an inspected partition table dump."""
    lines: list[str] = []
    active_id = summary["active_table_id"]
    active_table = summary["active_table"]
    lines.append(
        f"Active partition table: table{active_id} "
        f"(flash 0x{active_table['flash_offset']:08x}, version={active_table['version']}, "
        f"age={active_table['age']}, entries={active_table['entry_cnt']})"
    )

    for label in ("table0", "table1"):
        slot = summary["tables"].get(label)
        if slot is None:
            lines.append(f"  {label}: not present in dump")
        elif slot.get("status") == "erased":
            lines.append(f"  {label}: erased (0xFF) at flash 0x{slot['flash_offset']:08x}")
        else:
            lines.append(
                f"  {label}: OK at flash 0x{slot['flash_offset']:08x} "
                f"(age={slot['age']}, hdr_crc={slot['header_crc32']}, "
                f"entries_crc={slot['entries_crc32']})"
            )

    active_fw = summary.get("active_fw")
    if active_fw is not None:
        lines.append(
            f"Active FW slot: index={active_fw['active_index']} "
            f"@ 0x{active_fw['active_address']:08x} "
            f"(max=0x{active_fw['active_max_len']:x}, len=0x{active_fw['length']:x}, "
            f"slot0=0x{active_fw['slot0_address']:08x}, slot1=0x{active_fw['slot1_address']:08x})"
        )

    lines.append("")
    lines.append("Idx  Name      Type  Idx  Addr0       Max0        Addr1       Max1        Age")
    for entry in active_table["entries"]:
        lines.append(
            f"{entry['index']:>3}  {entry['name']:<8}  {entry['type']:>4}  "
            f"{entry['active_index']:>3}  "
            f"0x{entry['start_address'][0]:08x}  0x{entry['max_len'][0]:08x}  "
            f"0x{entry['start_address'][1]:08x}  0x{entry['max_len'][1]:08x}  "
            f"{entry['age']}"
        )
    return "\n".join(lines)


def _parse_int_auto(value: str) -> int:
    return int(value, 0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Inspect and CRC-32 verify BL616 BFPT partition table flash dumps."
    )
    parser.add_argument("path", type=pathlib.Path, help="Path to flash or partition-table binary")
    parser.add_argument(
        "--offset",
        type=_parse_int_auto,
        default=None,
        help="Flash base offset of the dump (e.g. 0x0 or 0xe000; auto-detected by default)",
    )
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON")
    args = parser.parse_args(argv)

    data = args.path.read_bytes()
    try:
        summary = inspect_dump(data, base_offset=args.offset)
    except ValueError as exc:
        if args.json:
            print(json.dumps({"ok": False, "error": str(exc)}, indent=2))
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps({"ok": True, **summary}, indent=2))
    else:
        print(format_report(summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
