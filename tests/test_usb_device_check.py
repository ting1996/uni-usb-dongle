import unittest

from tools import usb_device_check, usb_ota


def make_state(capabilities=0, paired_slots=()):
    payload = bytearray(usb_device_check.DEVICE_STATE_SIZE)
    payload[0] = usb_device_check.STATUS_OK
    payload[1] = capabilities
    payload[2] = usb_device_check.NO_SELECTED_SLOT
    for slot in range(usb_device_check.CONTROLLER_SLOT_COUNT):
        offset = 3 + slot * 9
        payload[offset] = int(slot < len(paired_slots) and paired_slots[slot])
        payload[offset + 2] = slot
    return bytes(payload)


class FakeDevice:
    def __init__(self, capabilities=0, paired_slots=()):
        self.state = make_state(capabilities, paired_slots)
        self.responses = bytearray()
        self.commands = []

    def write(self, packet):
        command, sequence, payload = usb_ota.decode_frame(packet)
        self.commands.append(command)

        if command == usb_device_check.CMD_GET_DEVICE_STATE:
            response_payload = self.state
        elif command in (
            usb_device_check.CMD_START_DISCOVERY,
            usb_device_check.CMD_PAIR_CONTROLLER,
        ):
            response_payload = bytes((usb_device_check.STATUS_UNSUPPORTED,))
        elif command == usb_device_check.CMD_SELECT_CONTROLLER:
            response_payload = bytes(
                (usb_device_check.STATUS_NOT_FOUND, self.state[2])
            )
        elif command == usb_ota.CMD_PROBE:
            response_payload = bytes(
                (usb_ota.STATUS_INVALID_COMMAND, 0, 0, 0, 0)
            )
        else:
            raise AssertionError(f"unexpected command: {command}")

        self.responses.extend(
            usb_ota.encode_frame(
                command | usb_ota.RESPONSE_FLAG, sequence, response_payload
            )
        )
        return len(packet)

    def flush(self):
        pass

    def read(self, size):
        chunk = bytes(self.responses[:size])
        del self.responses[:size]
        return chunk


class UsbDeviceCheckTests(unittest.TestCase):
    def test_check_runs_only_safe_queries_when_radio_is_unavailable(self):
        device = FakeDevice()
        messages = []

        state = usb_device_check.check_device(
            device, timeout=0.1, output=messages.append
        )

        self.assertEqual(state.capabilities, 0)
        self.assertEqual(
            device.commands.count(usb_device_check.CMD_GET_DEVICE_STATE), 2
        )
        self.assertIn(usb_device_check.CMD_START_DISCOVERY, device.commands)
        self.assertIn(usb_device_check.CMD_PAIR_CONTROLLER, device.commands)
        self.assertIn(usb_ota.CMD_PROBE, device.commands)
        self.assertIn(
            "empty-slot selection: rejected without changing state", messages
        )

    def test_check_skips_radio_actions_when_bluetooth_is_available(self):
        device = FakeDevice(
            capabilities=usb_device_check.CAPABILITY_BLUETOOTH_HID,
            paired_slots=(True, False),
        )

        usb_device_check.check_device(device, timeout=0.1, output=lambda _: None)

        self.assertNotIn(usb_device_check.CMD_START_DISCOVERY, device.commands)
        self.assertNotIn(usb_device_check.CMD_PAIR_CONTROLLER, device.commands)
        self.assertIn(usb_device_check.CMD_SELECT_CONTROLLER, device.commands)


if __name__ == "__main__":
    unittest.main()
