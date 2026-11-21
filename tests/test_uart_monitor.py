import unittest
from tools.uart_monitor import open_uart


class UartMonitorTest(unittest.TestCase):
    def test_modem_lines_are_disabled_before_opening(self):
        class Port:
            dtr = rts = True
            def __init__(self, **kwargs):
                self.options = kwargs
            def open(self):
                self.opened = True
                if self.dtr or self.rts:
                    raise AssertionError("UART modem lines asserted during open")
        class Serial:
            Serial = Port
        port = open_uart(Serial, "/dev/ttyUSB0", 115200)
        self.assertTrue(port.opened)
        self.assertIsNone(port.options["port"])
        self.assertEqual(port.port, "/dev/ttyUSB0")
