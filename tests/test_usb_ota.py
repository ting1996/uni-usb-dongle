import struct
import unittest

from tools import usb_ota


def make_test_image(body=b"compressed-image"):
    header = bytearray(usb_ota.OTA_HEADER_SIZE)
    header[:9] = b"BL60X_OTA"
    header[16:18] = b"XZ"
    struct.pack_into("<I", header, 20, len(body))
    return bytes(header) + body


class FakeDevice:
    def __init__(self):
        self.responses = bytearray()
        self.image = bytearray()
        self.expected_offset = 0
        self.drop_first_data_ack = True

    def write(self, packet):
        command, sequence, payload = usb_ota.decode_frame(packet)
        status = usb_ota.STATUS_OK

        if command == usb_ota.CMD_BEGIN:
            self.expected_offset = usb_ota.OTA_HEADER_SIZE
            self.image.extend(payload[4:])
        elif command == usb_ota.CMD_DATA:
            if sequence + len(payload) == self.expected_offset:
                pass
            elif sequence != self.expected_offset:
                status = usb_ota.STATUS_BAD_OFFSET
            else:
                self.image.extend(payload)
                self.expected_offset += len(payload)
                if self.drop_first_data_ack:
                    self.drop_first_data_ack = False
                    return len(packet)
        elif command == usb_ota.CMD_FINISH:
            if sequence != self.expected_offset:
                status = usb_ota.STATUS_INCOMPLETE
        elif command == usb_ota.CMD_PROBE:
            status = usb_ota.STATUS_INVALID_COMMAND
        else:
            status = usb_ota.STATUS_INVALID_COMMAND

        response_payload = struct.pack("<BI", status, self.expected_offset)
        self.responses.extend(
            usb_ota.encode_frame(command | usb_ota.RESPONSE_FLAG,
                                 sequence, response_payload)
        )
        return len(packet)

    def flush(self):
        pass

    def read(self, size):
        chunk = bytes(self.responses[:size])
        del self.responses[:size]
        return chunk


class UsbOtaTests(unittest.TestCase):
    def test_frame_encoding_and_checksum(self):
        payload = bytes((0x10, 0x20, 0x30, 0x40))
        packet = usb_ota.encode_frame(
            usb_ota.CMD_DATA, 0x12345678, payload
        )
        self.assertEqual(
            packet.hex(),
            "554f544101027856341204003b3a53f710203040",
        )
        command, sequence, decoded_payload = usb_ota.decode_frame(packet)
        self.assertEqual(
            (command, sequence, decoded_payload),
            (usb_ota.CMD_DATA, 0x12345678, payload),
        )
        damaged = bytearray(packet)
        damaged[-1] ^= 0x01
        with self.assertRaisesRegex(ValueError, "checksum"):
            usb_ota.decode_frame(damaged)

    def test_image_header_is_validated_before_transfer(self):
        image = make_test_image()
        self.assertEqual(usb_ota.validate_image(image), len(image) - 512)
        with self.assertRaisesRegex(ValueError, "size"):
            usb_ota.validate_image(image[:-1])
        with self.assertRaisesRegex(ValueError, "XZ"):
            usb_ota.validate_image(image[:16] + b"RAW!" + image[20:])

    def test_upload_retries_a_lost_ack_without_duplicate_data(self):
        image = make_test_image(bytes(range(256)) * 3)
        device = FakeDevice()
        usb_ota.upload(device, image, chunk_size=128, retries=2, timeout=0.1)
        self.assertEqual(bytes(device.image), image)
        self.assertEqual(device.expected_offset, len(image))

    def test_probe_checks_protocol_without_starting_update(self):
        device = FakeDevice()
        usb_ota.probe(device, retries=0, timeout=0.1)
        self.assertEqual(device.image, bytearray())
        self.assertEqual(device.expected_offset, 0)

    def test_upload_rejects_invalid_retry_and_timeout_values(self):
        image = make_test_image()
        with self.assertRaisesRegex(ValueError, "retries"):
            usb_ota.upload(FakeDevice(), image, retries=-1)
        with self.assertRaisesRegex(ValueError, "timeout"):
            usb_ota.upload(FakeDevice(), image, timeout=0)


if __name__ == "__main__":
    unittest.main()
