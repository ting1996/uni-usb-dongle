#!/usr/bin/env python3
"""Transfer a Bouffalo XZ OTA package over native USB CDC-ACM."""

import argparse
import pathlib
import struct
import time
import zlib

MAGIC = b"UOTA"
VERSION = 1
HEADER_SIZE = 16
MAX_PAYLOAD_SIZE = 1024
OTA_HEADER_SIZE = 512
RESPONSE_FLAG = 0x80

CMD_BEGIN = 1
CMD_DATA = 2
CMD_FINISH = 3
CMD_ABORT = 4
CMD_PROBE = 0x7F
STATUS_OK = 0
STATUS_INVALID_FRAME = 1
STATUS_INVALID_COMMAND = 2
STATUS_BUSY = 3
STATUS_INVALID_IMAGE = 4
STATUS_UPDATE_FAILED = 5
STATUS_BAD_OFFSET = 6
STATUS_INCOMPLETE = 7


def encode_frame(command, sequence, payload=b""):
    if len(payload) > MAX_PAYLOAD_SIZE:
        raise ValueError("frame payload exceeds protocol limit")
    header = MAGIC + struct.pack("<BBIH", VERSION, command, sequence, len(payload))
    checksum = zlib.crc32(header[4:] + payload) & 0xFFFFFFFF
    return header + struct.pack("<I", checksum) + payload


def decode_frame(packet):
    if len(packet) < HEADER_SIZE or packet[:4] != MAGIC:
        raise ValueError("invalid OTA frame header")
    version, command, sequence, payload_size = struct.unpack_from("<BBIH", packet, 4)
    if version != VERSION:
        raise ValueError(f"unsupported OTA protocol version: {version}")
    if payload_size > MAX_PAYLOAD_SIZE or len(packet) != HEADER_SIZE + payload_size:
        raise ValueError("invalid OTA frame length")
    expected_checksum = struct.unpack_from("<I", packet, 12)[0]
    payload = packet[HEADER_SIZE:]
    actual_checksum = zlib.crc32(packet[4:12] + payload) & 0xFFFFFFFF
    if actual_checksum != expected_checksum:
        raise ValueError("OTA frame checksum mismatch")
    return command, sequence, payload


def validate_image(image):
    if len(image) < OTA_HEADER_SIZE:
        raise ValueError("OTA image is shorter than its 512-byte header")
    header = image[:OTA_HEADER_SIZE]
    if not header[:9] == b"BL60X_OTA":
        raise ValueError("unrecognized Bouffalo OTA image header")
    if header[16:18] != b"XZ":
        raise ValueError("only SDK XZ OTA packages are supported")
    body_size = struct.unpack_from("<I", header, 20)[0]
    if body_size == 0 or len(image) != OTA_HEADER_SIZE + body_size:
        raise ValueError("OTA package size does not match its header")
    return body_size


def _read_exact(port, size, deadline):
    data = bytearray()
    while len(data) < size:
        if time.monotonic() >= deadline:
            raise TimeoutError("timed out receiving OTA response")
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def read_response(port, timeout):
    deadline = time.monotonic() + timeout
    window = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        window.extend(byte)
        if len(window) > len(MAGIC):
            del window[0]
        if bytes(window) != MAGIC:
            continue

        try:
            suffix = _read_exact(port, HEADER_SIZE - len(MAGIC), deadline)
            payload_size = struct.unpack_from("<H", suffix, 6)[0]
            if payload_size > MAX_PAYLOAD_SIZE:
                window.clear()
                continue
            payload = _read_exact(port, payload_size, deadline)
            return decode_frame(MAGIC + suffix + payload)
        except ValueError:
            window.clear()
    raise TimeoutError("timed out waiting for OTA response")


def _request(port, command, sequence, payload, retries, timeout,
             accepted_statuses=(STATUS_OK,)):
    packet = encode_frame(command, sequence, payload)
    last_timeout = None
    for _ in range(retries + 1):
        port.write(packet)
        port.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                response_command, response_sequence, response_payload = read_response(
                    port, deadline - time.monotonic()
                )
            except TimeoutError as error:
                last_timeout = error
                break
            if (response_command != (command | RESPONSE_FLAG) or
                    response_sequence != sequence):
                continue
            if len(response_payload) != 5:
                raise ValueError("invalid OTA response payload")
            status, next_offset = struct.unpack("<BI", response_payload)
            if status not in accepted_statuses:
                raise RuntimeError(
                    f"device rejected OTA command {command} with status {status}"
                )
            return next_offset
    message = f"no response to OTA command {command} after {retries + 1} attempts"
    if last_timeout is not None:
        message += f" ({last_timeout})"
    raise TimeoutError(message)


def probe(port, retries=3, timeout=3.0):
    """Check USB framing without starting an update or touching flash."""
    next_offset = _request(
        port, CMD_PROBE, 0, b"", retries, timeout,
        accepted_statuses=(STATUS_INVALID_COMMAND,),
    )
    if next_offset != 0:
        raise RuntimeError(f"unexpected OTA probe response offset: {next_offset}")


def upload(port, image, chunk_size=512, retries=3, timeout=3.0):
    validate_image(image)
    if not 1 <= chunk_size <= MAX_PAYLOAD_SIZE:
        raise ValueError(f"chunk size must be 1..{MAX_PAYLOAD_SIZE}")
    if retries < 0:
        raise ValueError("retries must not be negative")
    if timeout <= 0:
        raise ValueError("timeout must be positive")

    begin_payload = struct.pack("<I", len(image)) + image[:OTA_HEADER_SIZE]
    next_offset = _request(port, CMD_BEGIN, 0, begin_payload, retries, timeout)
    if next_offset != OTA_HEADER_SIZE:
        raise RuntimeError(f"unexpected initial OTA offset: {next_offset}")

    for offset in range(OTA_HEADER_SIZE, len(image), chunk_size):
        chunk = image[offset:offset + chunk_size]
        next_offset = _request(port, CMD_DATA, offset, chunk, retries, timeout)
        if next_offset != offset + len(chunk):
            raise RuntimeError(f"device acknowledged unexpected OTA offset: {next_offset}")

    next_offset = _request(port, CMD_FINISH, len(image), b"", retries, timeout)
    if next_offset != len(image):
        raise RuntimeError(f"device acknowledged unexpected final offset: {next_offset}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        required=True,
        help="native USB CDC-ACM port, e.g. COM5 or /dev/ttyACM0",
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--image", type=pathlib.Path, help="SDK-generated .xz.ota image"
    )
    mode.add_argument(
        "--probe",
        action="store_true",
        help="test the USB OTA protocol without starting an update or writing flash",
    )
    parser.add_argument("--chunk-size", type=int, default=512)
    parser.add_argument("--retries", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()

    image = None
    if not args.probe:
        image = args.image.read_bytes()
        validate_image(image)

    try:
        import serial
    except ImportError as error:
        raise SystemExit("Install pyserial first: python3 -m pip install pyserial") from error

    with serial.Serial(args.port, timeout=0.1, write_timeout=10) as port:
        port.dtr = False
        port.rts = False
        time.sleep(0.5)
        if args.probe:
            print(f"Probing native USB OTA protocol on {args.port} (no flash writes)")
            probe(port, args.retries, args.timeout)
        else:
            print(f"Sending {len(image)} bytes to native USB port {args.port}")
            print("Keep the device powered and do not disconnect it during the update.")
            upload(port, image, args.chunk_size, args.retries, args.timeout)
    if args.probe:
        print("OTA protocol probe succeeded; no update was started.")
        return
    print("OTA accepted; the device should reboot into the updated image.")


if __name__ == "__main__":
    main()
